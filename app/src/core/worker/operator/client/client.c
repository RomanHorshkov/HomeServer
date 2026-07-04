/**
 * @file client.c
 * @brief Per-connection I/O and HTTP parsing loop executed inside an operator.
 *
 * Each client owns one cumulative receive buffer and one DB_http parser handle.
 * DB_http stores zero-copy string views into the cumulative buffer, so this
 * layer appends new bytes, feeds only the new byte count, and keeps the buffer
 * stable while the parsed request snapshot is inspected by higher layers.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE */

#include "client.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <DB_http/DB_http.h>
#include <emlog.h>

#include "socket_helper.h"
#include "time_helper.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "srv_client"

/****************************************************************************
 * PRIVATE FUNCTION DECLARATIONS
 ****************************************************************************
 */

static void _client_reset_message(client_t *cli, int clear_stored_request);
static int  _client_store_request(client_t *cli, const DB_http_request_t *req, uint8_t thread_id);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int client_handle(client_t *cli, uint8_t thread_id)
{
    if(!cli || !cli->http_parser || cli->ctx.fd < 0)
    {
        EML_ERROR(LOG_TAG, "client_handle: invalid input");
        return STATUS_FAILURE;
    }

    for(;;)
    {
        if(cli->buf_idx >= DB_HTTP_MAX_BUFFER_LEN_B)
        {
            EML_ERROR(LOG_TAG, "fd %d: receive buffer limit reached", cli->ctx.fd);
            goto fail;
        }

        const size_t writable = DB_HTTP_MAX_BUFFER_LEN_B - cli->buf_idx;
        ssize_t read_bytes = read(cli->ctx.fd, cli->buf + cli->buf_idx, writable);

        if(read_bytes > 0)
        {
            DB_http_request_t *parsed_req = NULL;
            const size_t new_bytes = (size_t)read_bytes;

            cli->last_activity = (uint64_t)time_helper_get_now();
            cli->buf_idx += new_bytes;

#ifdef DEBUG
            EML_DEBUG(LOG_TAG, "fd %d received %zu bytes, buffered=%zu",
                      cli->ctx.fd, new_bytes, cli->buf_idx);
#endif

            DB_http_status_t parse_status = db_http_parser_exec(cli->http_parser, cli->buf, new_bytes, &parsed_req);
            if(parse_status != DB_http_status_OK)
            {
                EML_ERROR(LOG_TAG, "fd %d: HTTP parse failed with status %d", cli->ctx.fd, parse_status);
                goto fail;
            }

            if(!parsed_req)
            {
                EML_ERROR(LOG_TAG, "fd %d: DB_http returned no request DTO", cli->ctx.fd);
                goto fail;
            }

            if(parsed_req->message_complete)
            {
                if(_client_store_request(cli, parsed_req, thread_id) != STATUS_SUCCESS)
                {
                    goto fail;
                }

                db_http_parser_clear(cli->http_parser);
                cli->buf_idx = 0u;

                /* Set Http time as last activity */
                parsed_req->timestamp = cli->last_activity;

#ifdef DEBUG
                EML_DEBUG(LOG_TAG, "fd %d parsed request: method=%u path=%.*s headers=%u body=%zu policy=%u",
                          cli->ctx.fd,
                          (unsigned)cli->http_request.method,
                          (int)cli->http_request.path.n,
                          cli->http_request.path.p ? cli->http_request.path.p : "",
                          (unsigned)cli->http_request.header_count,
                          cli->http_request.body.n,
                          (unsigned)cli->http_request.connection_policy);
#endif

                return STATUS_SUCCESS;
            }

            continue;
        }

        if(read_bytes == 0)
        {
            EML_INFO(LOG_TAG, "fd %d: peer closed connection", cli->ctx.fd);
            goto fail;
        }

        if(errno == EINTR)
        {
            continue;
        }

        if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return STATUS_SUCCESS;
        }

        EML_PERR(LOG_TAG, "fd %d: read failed", cli->ctx.fd);
        goto fail;
    }

fail:
    _client_reset_message(cli, 1);
    return STATUS_FAILURE;
}

void client_shutdown(client_t *cli)
{
    if(!cli)
    {
        EML_ERROR(LOG_TAG, "shutdown: invalid input");
        return;
    }

    if(cli->is_busy)
    {
        _client_reset_message(cli, 1);

        socket_shutdown_and_close(cli->ctx.fd);
        cli->ctx.fd = -1;
        cli->ctx.owner = NULL;
        cli->ctx.handler = NULL;

        cli->is_busy = 0u;
        cli->connection_policy = 0u;
        cli->last_activity = 0u;
        cli->request_count = 0u;
    }
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int _client_store_request(client_t *cli, const DB_http_request_t *req, uint8_t thread_id)
{
    if(!cli || !req || !req->message_complete)
    {
        EML_ERROR(LOG_TAG, "store_request: invalid input");
        return STATUS_FAILURE;
    }

    cli->http_request = *req;
    cli->http_request.thread_id = thread_id;
    cli->http_request.timestamp = cli->last_activity;

    /* TODO: populate peer address metadata when accept()/getpeername() state is wired into client_t. */
    cli->http_request.remote_ip_be = 0u;
    cli->http_request.remote_port_be = 0u;

    cli->connection_policy = cli->http_request.connection_policy;
    cli->request_count++;

    return STATUS_SUCCESS;
}

static void _client_reset_message(client_t *cli, int clear_stored_request)
{
    if(!cli)
    {
        return;
    }

    if(cli->http_parser)
    {
        db_http_parser_clear(cli->http_parser);
    }

    cli->buf_idx = 0u;
    memset(cli->buf, 0, sizeof(cli->buf));

    if(clear_stored_request)
    {
        memset(&cli->http_request, 0, sizeof(cli->http_request));
    }
}
