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

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "srv_client"

/****************************************************************************
 * PRIVATE FUNCTION DECLARATIONS
 ****************************************************************************
 */

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
int client_handle(client_t *cli)
{
    if(!cli)
    {
        EML_ERROR(LOG_TAG, "client_handle: invalid input");
        return STATUS_FAILURE;
    }

    llhttp_parser_ctx_t *p_ctx = &cli->http_parser.parser_ctx;
    size_t start_buf_idx = 0;

    /* If already in parsing state, start writing the buffer where stopped before */
    if(p_ctx->parsing) start_buf_idx = p_ctx->buf_used;

    /* This way can set the kernel to receive (write) consecutively on the same buffer */
    ssize_t read_bytes = socket_read_nonblocking(cli->ctx.fd, cli->recv_buf + start_buf_idx, HTTP_RECEIVE_BUFFER_LEN - start_buf_idx);

    if(read_bytes > 0)
    {
#ifdef DEBUG_MODE
        EML_DBG(LOG_TAG, "fd %d received %zd bytes, executing http parser", cli->ctx.fd, read_bytes);
#endif
        /* Set client's last activity */
        cli->last_activity = (uint32_t)time_helper_get_now();

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

            /* Call the router */

            /* Send the response */


            // HttpResponse response = {0};

            // int route_rc = router_handle_request(&cli->http_parser.req, &response);

            // if(route_rc != STATUS_SUCCESS && response.status_code == 0)
            // {
            //     EML_ERROR(LOG_TAG, "router_handle_request failed for fd %d", cli->ctx.fd);
            //     _fill_500(&response);
            // }

            // if(_send_response(cli->ctx.fd, &cli->http_parser.req, &response) != STATUS_SUCCESS)
            // {
            //     _free_response_body(&response);
            //     return STATUS_FAILURE;
            // }

            // _free_response_body(&response);
            // http_man_reset(&cli->http_parser);

            // if(cli->http_parser.req.connection_policy == HTTP_CONNECTION_CLOSE)
            // {
            //     return STATUS_FAILURE; /* close after reply */
            // }

            /* Clean buffers */
            
            /* After using the request reset parser for next message */
            http_man_reset(&cli->http_parser);

            /* Clean client's recv buffer and reset status */
            cli->last_activity = 0;
            cli->request_count = 0;
            memset(cli->recv_buf, 0, HTTP_RECEIVE_BUFFER_LEN);

            return STATUS_SUCCESS;
        }
        else
        {
            EML_ERROR(LOG_TAG, "fd %d: unexpected parser state after execute", cli->ctx.fd);
            goto hell;
        }
    }
    else
    {
        EML_ERROR(LOG_TAG, "socket_read_nonblocking failed for fd %d", cli->ctx.fd);
        goto hell;
    }
    

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
        http_man_reset(&cli->http_parser);
        socket_shutdown_and_close(cli->ctx.fd);
        cli->is_busy = 0;
        cli->last_activity = 0;
        cli->request_count = 0;
        memset(cli->recv_buf, 0, HTTP_RECEIVE_BUFFER_LEN);
    }
}

// static int _send_response(int fd, const Http_request_t *req, const HttpResponse *resp)
// {
//     if(!resp) return STATUS_FAILURE;

//     const int status = resp->status_code ? resp->status_code : 500;
//     const char *reason = resp->status_text ? resp->status_text : "OK";
//     const char *ctype = resp->content_type ? resp->content_type : "application/octet-stream";
//     const char *conn =
//         (req && req->connection_policy == HTTP_CONNECTION_CLOSE) ? "close" : "keep-alive";

//     char hdr_buf[2048];
//     size_t offset = 0;

//     int written = snprintf(hdr_buf, sizeof(hdr_buf),
//                            "HTTP/1.1 %d %s\r\n"
//                            "Content-Type: %s\r\n"
//                            "Content-Length: %zu\r\n",
//                            status,
//                            reason,
//                            ctype,
//                            resp->body ? resp->body_length : 0);
//     if(written < 0 || (size_t)written >= sizeof(hdr_buf)) return STATUS_FAILURE;
//     offset = (size_t)written;

//     for(int i = 0; i < resp->header_count; ++i)
//     {
//         written = snprintf(hdr_buf + offset,
//                            sizeof(hdr_buf) - offset,
//                            "%s: %s\r\n",
//                            resp->header_names[i],
//                            resp->header_values[i]);
//         if(written < 0 || (size_t)written >= sizeof(hdr_buf) - offset) return STATUS_FAILURE;
//         offset += (size_t)written;
//     }

//     written = snprintf(hdr_buf + offset,
//                        sizeof(hdr_buf) - offset,
//                        "Connection: %s\r\n\r\n",
//                        conn);
//     if(written < 0 || (size_t)written >= sizeof(hdr_buf) - offset) return STATUS_FAILURE;
//     offset += (size_t)written;

//     if(_send_all(fd, hdr_buf, offset) < 0) return STATUS_FAILURE;
//     if(resp->body && resp->body_length > 0)
//     {
//         if(_send_all(fd, resp->body, resp->body_length) < 0) return STATUS_FAILURE;
//     }

//     return STATUS_SUCCESS;
// }

// static ssize_t _send_all(int fd, const void *buf, size_t len)
// {
//     const char *ptr = (const char *)buf;
//     size_t total = 0;

//     while(total < len)
//     {
//         ssize_t sent = send(fd, ptr + total, len - total, 0);
//         if(sent < 0)
//         {
//             if(errno == EINTR) continue;
//             return -1;
//         }
//         total += (size_t)sent;
//     }

//     return (ssize_t)total;
// }

// static void _free_response_body(HttpResponse *resp)
// {
//     if(!resp) return;
//     if(resp->body_owned && resp->body)
//     {
//         free(resp->body);
//     }
//     resp->body = NULL;
//     resp->body_length = 0;
//     resp->body_owned = 0;
// }
