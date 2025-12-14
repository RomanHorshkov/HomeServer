/**
 * @file client.c
 * @brief Per‑connection I/O and HTTP parsing loop executed inside an operator.
 *
 * Each operator holds an array of client slots. When epoll marks a socket
 * readable the operator invokes ::client_handle() which performs a bounded
 * recv()/parse loop:
 *   1. Read as much as fits in the slot buffer (non‑blocking).
 *   2. Feed bytes to the HTTP parser (llhttp wrapper) maintaining incremental state.
 *   3. On complete message: log request metadata, reset parser, and (currently)
 *      request connection closure (placeholder until routing layer integration).
 *
 * Design notes:
 * - Lock‑free: no shared state is mutated outside the owning operator thread.
 * - Backpressure: returns STATUS_SUCCESS when socket would block so reactor
 *   can yield; STATUS_FAILURE signals disposal (EOF / parse error / policy).
 * - Parser reset: we reset after a full message to support pipelining later;
 *   close-on-first-request is a temporary simplification.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE */

#include "client.h"

#include "emlog.h"

// #include "http_manager.h" included in .h
#include "router.h"
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

inline static void _client_cleanup(client_t *cli);

// static int _send_response(int fd, const http_request_t *req, const HttpResponse *resp);
// static ssize_t _send_all(int fd, const void *buf, size_t len);
// static void _free_response_body(HttpResponse *resp);
// static void _fill_500(HttpResponse *resp);


/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */


/**
 * @brief Drain a readable client socket and advance HTTP parsing state.
 *
 * Loop semantics:
 *   - Continues reading while recv() yields positive byte counts.
 *   - Stops with STATUS_SUCCESS on EAGAIN/EWOULDBLOCK/EINTR (socket will remain).
 *   - Returns STATUS_FAILURE on EOF, fatal recv error, or HTTP parse failure.
 *   - On complete HTTP request: logs details, resets parser, and returns
 *     STATUS_FAILURE (current policy: close after one request).
 *
 * Timing/accounting:
 *   - Updates slot->last_activity with coarse millisecond timestamp.
 *   - Increments slot->request_count for timeout heuristics.
 *
 * @param slot Client slot containing parser state and buffers.
 * @return STATUS_SUCCESS to keep connection; STATUS_FAILURE to drop it.
 */
int client_handle(client_t *cli, uint8_t thread_id)
{
    if(!cli)
    {
        EML_ERROR(LOG_TAG, "client_handle: invalid input");
        return STATUS_FAILURE;
    }

    llhttp_parser_ctx_t *p_ctx = &cli->http_parser.parser_ctx;
    size_t start_buf_idx = 0;

    /* If already in parsing state, continue writing after the bytes already present */
    if(p_ctx->parsing) start_buf_idx = p_ctx->buf_used;

    /* Ensure we always receive into the same stable buffer and avoid overrun */
    if(start_buf_idx >= HTTP_RECV_BUFFER_LEN)
    {
        EML_ERROR(LOG_TAG, "recv buffer overflow for fd %d", cli->ctx.fd);
        goto hell;
    }

    /* Set client's last activity before even parsing or collecting data,
    at epoll trigger set the client's last_activity */
    cli->last_activity = (uint64_t)time_helper_get_now();

    size_t available_space = HTTP_RECV_BUFFER_LEN - start_buf_idx;
    ssize_t read_bytes = socket_read_nonblocking(cli->ctx.fd, cli->recv_buf + start_buf_idx, available_space);

    if(read_bytes <= 0)
    {
        
        EML_ERROR(LOG_TAG, "fd %d: unexpected parser state after execute", cli->ctx.fd);
        goto hell;
    }
#ifdef MODE_DEBUG
    EML_DBG(LOG_TAG, "fd %d received %zd bytes, executing http parser", cli->ctx.fd, read_bytes);
#endif

    /* Execute http parser over the request and store the state if msg is incomplete */
    if(http_man_execute(&cli->http_parser, cli->recv_buf, (size_t)read_bytes) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "HTTP parse failed on fd %d", cli->ctx.fd);
        goto hell;
    }

    /* If the message is not complete, wait for more data */
    if(!p_ctx->req.message_complete && p_ctx->parsing)
    {
        EML_DBG(LOG_TAG, "fd %d: message not complete yet, waiting for more data", cli->ctx.fd);
        return STATUS_SUCCESS;  /* need more data */
    }

    /* At this point ctx->req is fully populated and sanitized */
    if(p_ctx->req.message_complete && !p_ctx->parsing)
    {
        /* Increase completed request counts */
        cli->request_count++;

        /* Prepare the request context for routing */
        http_request_t *req = &p_ctx->req;
        req->thread_id = thread_id;
        req->timestamp = cli->last_activity;

        /* TODO:
        set ip and port */
        req->remote_ip_be = 0;
        req->remote_port_be = 0;

        /* Everything else of the request is filled up by llhttp parser */

        /* Call the router */
        if(router_handle_request(req, &cli->send_resp) != STATUS_SUCCESS)
        {
            EML_ERROR(LOG_TAG, "router_handle_request failed for fd %d", cli->ctx.fd);
            goto hell;
        }
        
        /* Send the client's correct response */
        EML_WARN(LOG_TAG, "Response sending not implemented yet.");

        /* TODO: Implement response sending */
        EML_DBG(LOG_TAG, "response: %s", cli->send_resp.send_sv.p);

        /* Clean buffers
        After using the request reset parser for next message
        and clean the client's receive AND SEND buffer */
        _client_cleanup(cli);

        return STATUS_SUCCESS;
    }

    EML_ERROR(LOG_TAG, "socket_read_nonblocking failed for fd %d", cli->ctx.fd);

hell:
    client_shutdown(cli);
    return STATUS_FAILURE;
}

void client_shutdown(client_t *cli)
{
    if(!cli)
    {
        EML_ERROR(LOG_TAG, "_shutdown: invalid input");
    }

    if(cli->is_busy)
    {
        _client_cleanup(cli);

        socket_shutdown_and_close(cli->ctx.fd);
        cli->is_busy = 0;
        cli->last_activity = 0;
        cli->request_count = 0;
        sv_reset(&cli->send_resp.send_sv);
        cli->send_resp.status_code = 0;
    }
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

inline static void _client_cleanup(client_t *cli)
{
    /* After using the request reset parser for next message */
    http_man_reset(&cli->http_parser);

    /* Clean client's recv buffer and hold the status */
    memset(cli->recv_buf, 0, HTTP_RECV_BUFFER_LEN);

    /* Clean client's send buffer */
    memset(cli->send_buf, 0, HTTP_SEND_BUFFER_LEN);
}
