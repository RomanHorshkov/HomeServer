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
#include <time.h>
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

/** Absolute ceiling for the whole body transfer (CLOCK_MONOTONIC) — defeats a never-idle-but-forever-slow client
 *  that the per-read idle timeout alone would let dribble forever. */
#define UPLOAD_MAX_WALL_S      WORKER_UPLOAD_MAX_WALL_S

/** How long / how much to drain an unread body after an early reject, so nginx can relay our real
 *  status instead of a broken-pipe 503. nginx stops sending the moment it reads our final response,
 *  so these are just safety ceilings — the drain normally ends at EOF within a couple of reads. */
#define UPLOAD_REJECT_DRAIN_MS    2000
#define UPLOAD_REJECT_DRAIN_BYTES (4u * 1024u * 1024u)

/** @brief Milliseconds on CLOCK_MONOTONIC (immune to wall-clock steps). */
static uint64_t _pump_mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/**
 * @brief Absorb the in-flight request body after an early reject, so the peer relays our response.
 *
 * The response is already written; we just read + discard whatever nginx is still streaming (with
 * proxy_request_buffering off it cannot stop until it has read our reply). Bounded by both a short
 * deadline and a byte ceiling — nginx stops sending as soon as it reads the final response, so this
 * returns at EOF almost immediately; the bounds only cap a misbehaving/huge peer.
 */
static void _drain_rejected_body(client_t* cli)
{
    const uint64_t deadline = _pump_mono_ms() + (uint64_t)UPLOAD_REJECT_DRAIN_MS;
    uint64_t       drained  = 0u;
    while(drained < (uint64_t)UPLOAD_REJECT_DRAIN_BYTES)
    {
        const uint64_t now = _pump_mono_ms();
        if(now >= deadline) break;
        struct pollfd pfd = {.fd = cli->ctx.fd, .events = POLLIN};
        int           prc = poll(&pfd, 1, (int)(deadline - now));
        if(prc <= 0) break; /* timeout, or poll error → stop (peer is done or gone) */
        ssize_t got = read(cli->ctx.fd, cli->buf, sizeof(cli->buf));
        if(got > 0)
        {
            drained += (uint64_t)got;
            continue;
        }
        if(got < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        break; /* EOF (nginx finished) or a real error */
    }
}

/** The exact public path the gate claims (query text never appears — nginx proxies the location match verbatim).
 *  Distinct one-shot upload endpoint (DB_server/README.md): the list is GET /api/app/files, the streaming
 *  upload is POST /api/app/uploads — so nginx routes the upload to its own upstream by URI, method-independently. */
#define UPLOAD_PATH          "/api/app/uploads"

/** The RESUMABLE-upload chunk endpoint: PUT /api/app/uploads/<id>/data — same upload upstream, streamed body. */
#define UPLOAD_DATA_PREFIX   "/api/app/uploads/"
#define UPLOAD_DATA_SUFFIX   "/data"

/** A single resumable chunk is bounded (the session's chunk_size is 16 MiB; allow generous slack, reject the absurd early). */
#define UPLOAD_DATA_CHUNK_MAX (32u * 1024u * 1024u)

/** @brief True when `path` is exactly "/api/app/uploads/<non-empty>/data" (query text never reaches the gate). */
static int _is_data_path(sv_t path)
{
    const size_t plen = sizeof UPLOAD_DATA_PREFIX - 1u;
    const size_t slen = sizeof UPLOAD_DATA_SUFFIX - 1u;
    if(path.n <= plen + slen)
    {
        return 0; /* need at least one <id> byte between prefix and suffix */
    }
    if(memcmp(path.p, UPLOAD_DATA_PREFIX, plen) != 0)
    {
        return 0;
    }
    return memcmp(path.p + path.n - slen, UPLOAD_DATA_SUFFIX, slen) == 0;
}

/*****************************************************************************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

int upload_stream_gate(uint8_t method, sv_t path, uint64_t content_length, void* ctx)
{
    (void)ctx;
    const db_app_platform_cfg_t* cfg = db_app_platform_cfg();
    if(!cfg)
    {
        return 0;
    }

    /* One-shot upload: POST /api/app/uploads (whole file streamed). Oversize declarations are DECLINED, not claimed
     * (the parser's own ContentTooLarge answer, mapped to 413, handles them). */
    if(method == (uint8_t)HTTP_METHOD_POST && path.n == sizeof UPLOAD_PATH - 1u && memcmp(path.p, UPLOAD_PATH, path.n) == 0)
    {
        return (content_length > 0u && content_length <= cfg->upload_max_bytes) ? 1 : 0;
    }

    /* Resumable chunk: PUT /api/app/uploads/<id>/data (one chunk streamed into a durable session, § transfer arc). */
    if(method == (uint8_t)HTTP_METHOD_PUT && _is_data_path(path))
    {
        return (content_length > 0u && content_length <= UPLOAD_DATA_CHUNK_MAX) ? 1 : 0;
    }

    return 0;
}

/** @brief Outcome of the shared body pump: fed the whole body / sent a terse error / peer vanished (no answer possible). */
typedef enum
{
    PUMP_OK = 0,
    PUMP_ANSWERED,
    PUMP_GONE
} pump_result_t;

/**
 * @brief The socket→sink body pump shared by BOTH upload paths (one-shot + resumable chunk).
 *
 * Feeds the already-buffered `leftover` then poll/reads the rest of `total` body bytes, handing each ≤32 KiB slice to
 * `chunk(sink,…)`. On ANY failure it sends the terse transport/reject answer itself (into @p out_rc) and returns
 * PUMP_ANSWERED — EXCEPT a mid-body peer close, which nobody can answer (PUMP_GONE, out_rc=STATUS_FAILURE). It NEVER
 * touches the sink's begin/commit/abort — the caller owns those (so the same loop drives a ticket or a data handle).
 */
static pump_result_t _pump_body(client_t* cli, const char* leftover, size_t leftover_len, uint64_t total, void* sink,
                                db_app_status_t (*chunk)(void*, const uint8_t*, size_t), int* out_rc)
{
    if(leftover_len > 0u && chunk(sink, (const uint8_t*)leftover, leftover_len) != DB_APP_OK)
    {
        *out_rc = response_writer_error(cli, 400u);
        return PUMP_ANSWERED;
    }

    const uint64_t deadline_ms = _pump_mono_ms() + (uint64_t)UPLOAD_MAX_WALL_S * 1000u;
    uint64_t       remaining   = total - (uint64_t)leftover_len;
    while(remaining > 0u)
    {
        const uint64_t now_ms = _pump_mono_ms();
        if(now_ms >= deadline_ms)
        {
            EML_WARN(LOG_TAG, "fd %d: upload exceeded %us wall deadline with %llu bytes missing — aborting", cli->ctx.fd,
                     UPLOAD_MAX_WALL_S, (unsigned long long)remaining);
            *out_rc = response_writer_error(cli, 408u);
            return PUMP_ANSWERED;
        }
        const uint64_t left_ms = deadline_ms - now_ms;
        const int      timeout = left_ms < (uint64_t)UPLOAD_READ_TIMEOUT_MS ? (int)left_ms : UPLOAD_READ_TIMEOUT_MS;

        struct pollfd pfd = {.fd = cli->ctx.fd, .events = POLLIN};
        int           prc = poll(&pfd, 1, timeout);
        if(prc == 0)
        {
            EML_WARN(LOG_TAG, "fd %d: upload stalled %d ms with %llu bytes missing — aborting", cli->ctx.fd, timeout,
                     (unsigned long long)remaining);
            *out_rc = response_writer_error(cli, 400u);
            return PUMP_ANSWERED;
        }
        if(prc < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            EML_PERR(LOG_TAG, "fd %d: upload poll failed", cli->ctx.fd);
            *out_rc = response_writer_error(cli, 500u);
            return PUMP_ANSWERED;
        }

        const size_t want = remaining < (uint64_t)sizeof(cli->buf) ? (size_t)remaining : sizeof(cli->buf);
        ssize_t      got  = read(cli->ctx.fd, cli->buf, want);
        if(got == 0)
        {
            EML_WARN(LOG_TAG, "fd %d: peer closed mid-upload with %llu bytes missing", cli->ctx.fd, (unsigned long long)remaining);
            *out_rc = STATUS_FAILURE; /* nobody left to answer */
            return PUMP_GONE;
        }
        if(got < 0)
        {
            if(errno == EINTR || errno == EAGAIN)
            {
                continue;
            }
            EML_PERR(LOG_TAG, "fd %d: upload read failed", cli->ctx.fd);
            *out_rc = response_writer_error(cli, 500u);
            return PUMP_ANSWERED;
        }

        if(chunk(sink, (const uint8_t*)cli->buf, (size_t)got) != DB_APP_OK)
        {
            *out_rc = response_writer_error(cli, 400u);
            return PUMP_ANSWERED;
        }
        remaining -= (uint64_t)got;
        cli->last_activity = (uint64_t)time_helper_get_now();
    }
    return PUMP_OK;
}

/* Thin sink adapters so the shared pump is sink-agnostic (a one-shot ticket vs a resumable data handle). */
static db_app_status_t _chunk_oneshot(void* s, const uint8_t* b, size_t n)
{
    return db_app_upload_chunk((db_app_upload_ticket_t*)s, b, n);
}
static db_app_status_t _chunk_data(void* s, const uint8_t* b, size_t n)
{
    return db_app_upload_data_chunk((db_app_upload_data_t*)s, b, n);
}

/** @brief One resumable chunk: PUT /api/app/uploads/<id>/data. Adapted @p req in hand; body streams into the durable session. */
static int _client_upload_data_pump(client_t* cli, DB_app_request_t* req)
{
    /* Authorize + resolve the session + reopen the spool at `received` (DB_app decides everything). */
    DB_app_response_t     res;
    db_app_response_init(&res);
    db_app_upload_data_t* h = NULL;
    if(db_app_upload_data_begin(req, &res, &h) != DB_APP_OK)
    {
        EML_WARN(LOG_TAG, "fd %d: upload-data rejected at begin (status %u)", cli->ctx.fd, res.status);
        int rc = response_writer_send(cli, &res);
        db_app_response_clear(&res);
        _drain_rejected_body(cli); /* same reason as the one-shot: let nginx relay our real status */
        return rc;
    }
    db_app_response_clear(&res);

    const char* leftover     = NULL;
    size_t      leftover_len = 0u;
    uint64_t    total        = 0u;
    if(db_http_parser_stream_info(cli->http_parser, &leftover, &leftover_len, &total) != DB_http_status_OK ||
       (uint64_t)leftover_len > total)
    {
        EML_ERROR(LOG_TAG, "fd %d: stream info inconsistent (data)", cli->ctx.fd);
        db_app_upload_data_abort(h);
        return response_writer_error(cli, 500u);
    }

    int           rc = STATUS_FAILURE;
    pump_result_t pr = _pump_body(cli, leftover, leftover_len, total, h, _chunk_data, &rc);
    if(pr != PUMP_OK)
    {
        db_app_upload_data_abort(h);
        return rc;
    }

    /* Persist progress + answer {received,declared,next_expected} (consumes the handle). */
    db_app_response_init(&res);
    (void)db_app_upload_data_end(h, &res);
    rc = response_writer_send(cli, &res);
    db_app_response_clear(&res);
    return rc;
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

    /* Dispatch: the resumable chunk endpoint is a PUT (gated to …/uploads/<id>/data); the one-shot upload is a POST. */
    if(req.method == (uint8_t)HTTP_METHOD_PUT)
    {
        return _client_upload_data_pump(cli, &req);
    }

    /* 2. Authorize + open the spool ticket (full guard, replay spend, role, quota — DB_app decides everything). */
    DB_app_response_t       res;
    db_app_response_init(&res);
    db_app_upload_ticket_t* ticket = NULL;
    if(db_app_upload_begin(&req, &res, &ticket) != DB_APP_OK)
    {
        EML_WARN(LOG_TAG, "fd %d: upload rejected at begin (status %u)", cli->ctx.fd, res.status);
        int rc = response_writer_send(cli, &res);
        db_app_response_clear(&res);
        _drain_rejected_body(cli);
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

    /* 4. Pump the body through the shared loop. */
    int           rc = STATUS_FAILURE;
    pump_result_t pr = _pump_body(cli, leftover, leftover_len, total, ticket, _chunk_oneshot, &rc);
    if(pr != PUMP_OK)
    {
        db_app_upload_abort(ticket);
        return rc;
    }

    /* 5. Commit (consumes the ticket on every outcome) and send DB_app's exact answer. */
    db_app_response_init(&res);
    (void)db_app_upload_commit(ticket, &res);
    rc = response_writer_send(cli, &res);
    db_app_response_clear(&res);
    return rc;
}
