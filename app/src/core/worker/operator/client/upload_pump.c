/**
 * @file upload_pump.c
 * @brief The §9.4 streaming-upload pump: gate → headers-only authorize (spool ticket) → socket-to-spool pump → commit → tiny 201.
 *
 * nginx has already pre-authorized (auth_request) and buffered the whole body, so the loopback stream arrives at memory speed —
 * the bounded poll+read pump below never babysits a slow client. Body bytes go straight from the socket into DB_app's spool
 * ticket in ≤32 KiB slices; they never enter a request DTO and never grow a buffer. Every exit path either sent DB_app's exact
 * response or a terse transport error, and the connection always closes afterwards (uploads are one-shot).
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    jul 2026
 * (c) 2026
 */

/*****************************************************************************************************************************************
 * PRIVATE INCLUDES
 *****************************************************************************************************************************************
 */
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#include <DB_http/DB_http.h>
#include <emlog.h>

#include <db_app.h>
#include <db_app/files/files.h>
#include <db_app/platform/platform.h>

#include <db_server/core/worker/operator/client/response_writer.h>
#include <db_server/core/worker/operator/client/upload_pump.h>
#include <db_server/utils/time_helper.h>

/*****************************************************************************************************************************************
 * PRIVATE DEFINES
 *****************************************************************************************************************************************
 */
#define LOG_TAG              "srv_upload"

/** Per-read patience: nginx streams from its own buffer over loopback, so a silent 10 s means the transfer is dead. */
#define UPLOAD_READ_TIMEOUT_MS 10000

/** The exact public path the gate claims (query text never appears — nginx proxies the location match verbatim). */
#define UPLOAD_PATH          "/api/app/files"

/*****************************************************************************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

int upload_stream_gate(uint8_t method, sv_t path, uint64_t content_length, void* ctx)
{
    (void)ctx;
    if(method != (uint8_t)HTTP_METHOD_POST)
    {
        return 0;
    }
    if(path.n != sizeof UPLOAD_PATH - 1u || memcmp(path.p, UPLOAD_PATH, path.n) != 0)
    {
        return 0;
    }

    /* Oversize declarations are DECLINED, not claimed: the parser's own ContentTooLarge answer (mapped to 413) handles them. */
    const db_app_platform_cfg_t* cfg = db_app_platform_cfg();
    if(!cfg || content_length == 0u || content_length > cfg->upload_max_bytes)
    {
        return 0;
    }
    return 1;
}

int client_upload_pump(client_t* cli, uint8_t thread_id, const DB_http_request_t* parsed_req)
{
    if(!cli || !parsed_req)
    {
        EML_ERROR(LOG_TAG, "pump: invalid input");
        return STATUS_FAILURE;
    }

    /* 1. Adapt the request line + headers; the body never enters the DTO. */
    DB_app_request_t req;
    if(db_app_request_from_db_http_headers(parsed_req, &req) != 0)
    {
        EML_ERROR(LOG_TAG, "fd %d: upload adapt failed", cli->ctx.fd);
        return response_writer_error(cli, 500u);
    }
    req.thread_id = thread_id;
    req.now_unix  = (uint64_t)time_helper_get_now();

    /* 2. Authorize + open the spool ticket (full guard, replay spend, role, quota — DB_app decides everything). */
    DB_app_response_t       res;
    db_app_response_init(&res);
    db_app_upload_ticket_t* ticket = NULL;
    if(db_app_upload_begin(&req, &res, &ticket) != DB_APP_OK)
    {
        EML_WARN(LOG_TAG, "fd %d: upload rejected at begin (status %u)", cli->ctx.fd, res.status);
        int rc = response_writer_send(cli, &res);
        db_app_response_clear(&res);
        return rc;
    }
    db_app_response_clear(&res);

    /* 3. The bytes already buffered past the headers belong to the body. */
    const char* leftover     = NULL;
    size_t      leftover_len = 0u;
    uint64_t    total        = 0u;
    if(db_http_parser_stream_info(cli->http_parser, &leftover, &leftover_len, &total) != DB_http_status_OK ||
       (uint64_t)leftover_len > total)
    {
        EML_ERROR(LOG_TAG, "fd %d: stream info inconsistent", cli->ctx.fd);
        db_app_upload_abort(ticket);
        return response_writer_error(cli, 500u);
    }
    if(leftover_len > 0u && db_app_upload_chunk(ticket, (const uint8_t*)leftover, leftover_len) != DB_APP_OK)
    {
        db_app_upload_abort(ticket);
        return response_writer_error(cli, 400u);
    }

    /* 4. Pump the remainder: poll → read → chunk, reusing the client buffer (its header views are consumed; begin copied its keeps). */
    uint64_t remaining = total - (uint64_t)leftover_len;
    while(remaining > 0u)
    {
        struct pollfd pfd = {.fd = cli->ctx.fd, .events = POLLIN};
        int           prc = poll(&pfd, 1, UPLOAD_READ_TIMEOUT_MS);
        if(prc == 0)
        {
            EML_WARN(LOG_TAG, "fd %d: upload stalled %d ms with %llu bytes missing — aborting", cli->ctx.fd, UPLOAD_READ_TIMEOUT_MS,
                     (unsigned long long)remaining);
            db_app_upload_abort(ticket);
            return response_writer_error(cli, 400u);
        }
        if(prc < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            EML_PERR(LOG_TAG, "fd %d: upload poll failed", cli->ctx.fd);
            db_app_upload_abort(ticket);
            return response_writer_error(cli, 500u);
        }

        const size_t want = remaining < (uint64_t)sizeof(cli->buf) ? (size_t)remaining : sizeof(cli->buf);
        ssize_t      got  = read(cli->ctx.fd, cli->buf, want);
        if(got == 0)
        {
            EML_WARN(LOG_TAG, "fd %d: peer closed mid-upload with %llu bytes missing", cli->ctx.fd, (unsigned long long)remaining);
            db_app_upload_abort(ticket);
            return STATUS_FAILURE; /* nobody left to answer */
        }
        if(got < 0)
        {
            if(errno == EINTR || errno == EAGAIN)
            {
                continue;
            }
            EML_PERR(LOG_TAG, "fd %d: upload read failed", cli->ctx.fd);
            db_app_upload_abort(ticket);
            return response_writer_error(cli, 500u);
        }

        if(db_app_upload_chunk(ticket, (const uint8_t*)cli->buf, (size_t)got) != DB_APP_OK)
        {
            db_app_upload_abort(ticket);
            return response_writer_error(cli, 400u);
        }
        remaining -= (uint64_t)got;
        cli->last_activity = (uint64_t)time_helper_get_now();
    }

    /* 5. Commit (consumes the ticket on every outcome) and send DB_app's exact answer. */
    db_app_response_init(&res);
    (void)db_app_upload_commit(ticket, &res);
    int rc = response_writer_send(cli, &res);
    db_app_response_clear(&res);
    return rc;
}
