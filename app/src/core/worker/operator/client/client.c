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

#include "http_manager.h"
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

static int _send_response(int fd, const http_request_t *req, const HttpResponse *resp);
static ssize_t _send_all(int fd, const void *buf, size_t len);
static void _free_response_body(HttpResponse *resp);
static void _fill_500(HttpResponse *resp);


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

    ssize_t read_bytes = socket_read_nonblocking(cli->ctx.fd, cli->recv_buf, HTTP_RECEIVE_BUFFER_LEN);

    if(read_bytes > 0)
    {
#ifdef DEBUG_MODE
        EML_DBG(LOG_TAG, "fd %d received %zd bytes, executing http parser", cli->ctx.fd, read_bytes);
#endif
        cli->last_activity = (uint32_t)time_helper_get_now();
        if(http_parser_execute(&cli->http_parser, cli->recv_buf, (size_t)read_bytes) != STATUS_SUCCESS)
        {
            EML_ERROR(LOG_TAG, "HTTP parse failed on fd %d", cli->ctx.fd);
            return STATUS_FAILURE;
        }

        if(cli->http_parser.req.message_complete)
        {
#ifdef DEBUG_MODE
            EML_DBG(LOG_TAG, "fd %d, method %s, path %s, body_len=%zu",
                        cli->ctx.fd,
                        http_method_to_string(cli->http_parser.req.method),
                        cli->http_parser.req.path,
                        cli->http_parser.req.body_len);
            EML_DBG(LOG_TAG, "fd %d headers (%d):", cli->ctx.fd, cli->http_parser.req.header_count);
            for(int i = 0; i < cli->http_parser.req.header_count; ++i)
            {
                EML_DBG(LOG_TAG, "  %s: %s",
                        cli->http_parser.req.header_names[i],
                        cli->http_parser.req.header_values[i]);
            }
#endif
            /* Increase completed request counts */
            cli->request_count++;

            HttpResponse response = {0};

            int route_rc = router_handle_request(&cli->http_parser.req, &response);
            if(route_rc != STATUS_SUCCESS && response.status_code == 0)
            {
                EML_ERROR(LOG_TAG, "router_handle_request failed for fd %d", cli->ctx.fd);
                _fill_500(&response);
            }

            if(_send_response(cli->ctx.fd, &cli->http_parser.req, &response) != STATUS_SUCCESS)
            {
                _free_response_body(&response);
                return STATUS_FAILURE;
            }

            _free_response_body(&response);
            http_parser_reset(&cli->http_parser);
            
            if(cli->http_parser.req.connection_policy == HTTP_CONNECTION_CLOSE)
            {
                return STATUS_FAILURE; /* close after reply */
            }
            return STATUS_SUCCESS; /* keep alive */
        }
        else
        {
#ifdef MODE_DEBUG
            EML_DBG(LOG_TAG, "fd %d: message not complete yet", cli->ctx.fd);
#endif
            return STATUS_SUCCESS;  /* need more data */
        }
    }
    else if(read_bytes == 0)
    {
#ifdef DEBUG_MODE
        EML_DBG(LOG_TAG, "fd %d peer closed connection", cli->ctx.fd);
#endif
        /* peer closed */
        return STATUS_FAILURE;
    }
    else
    {
#ifdef DEBUG_MODE
        EML_PERR(LOG_TAG, "recv failed on fd %d", slot->fd);
#endif
        if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
#ifdef DEBUG_MODE
            EML_DBG(LOG_TAG, "fd %d recv would block, again, or eintr; yielding", slot->fd);
#endif
            return STATUS_SUCCESS;
        }
        EML_PERR(LOG_TAG, "recv failed on fd %d", slot->fd);
        return STATUS_FAILURE;
    }
}

void client_shutdown(client_t *cli)
{
    if(!cli)
    {
        EML_ERROR(LOG_TAG, "_shutdown: invalid input");
    }

    if (cli->is_busy)
    {
        socket_shutdown_and_close(cli->ctx.fd);
        http_parser_reset(&cli->http_parser);
        cli->is_busy = 0;
        cli->last_activity = 0;
        cli->request_count = 0;
        memset(cli->recv_buf, 0, HTTP_RECEIVE_BUFFER_LEN);
    }
}

static int _send_response(int fd, const Http_request_t *req, const HttpResponse *resp)
{
    if(!resp) return STATUS_FAILURE;

    const int status = resp->status_code ? resp->status_code : 500;
    const char *reason = resp->status_text ? resp->status_text : "OK";
    const char *ctype = resp->content_type ? resp->content_type : "application/octet-stream";
    const char *conn =
        (req && req->connection_policy == HTTP_CONNECTION_CLOSE) ? "close" : "keep-alive";

    char hdr_buf[2048];
    size_t offset = 0;

    int written = snprintf(hdr_buf, sizeof(hdr_buf),
                           "HTTP/1.1 %d %s\r\n"
                           "Content-Type: %s\r\n"
                           "Content-Length: %zu\r\n",
                           status,
                           reason,
                           ctype,
                           resp->body ? resp->body_length : 0);
    if(written < 0 || (size_t)written >= sizeof(hdr_buf)) return STATUS_FAILURE;
    offset = (size_t)written;

    for(int i = 0; i < resp->header_count; ++i)
    {
        written = snprintf(hdr_buf + offset,
                           sizeof(hdr_buf) - offset,
                           "%s: %s\r\n",
                           resp->header_names[i],
                           resp->header_values[i]);
        if(written < 0 || (size_t)written >= sizeof(hdr_buf) - offset) return STATUS_FAILURE;
        offset += (size_t)written;
    }

    written = snprintf(hdr_buf + offset,
                       sizeof(hdr_buf) - offset,
                       "Connection: %s\r\n\r\n",
                       conn);
    if(written < 0 || (size_t)written >= sizeof(hdr_buf) - offset) return STATUS_FAILURE;
    offset += (size_t)written;

    if(_send_all(fd, hdr_buf, offset) < 0) return STATUS_FAILURE;
    if(resp->body && resp->body_length > 0)
    {
        if(_send_all(fd, resp->body, resp->body_length) < 0) return STATUS_FAILURE;
    }

    return STATUS_SUCCESS;
}

static ssize_t _send_all(int fd, const void *buf, size_t len)
{
    const char *ptr = (const char *)buf;
    size_t total = 0;

    while(total < len)
    {
        ssize_t sent = send(fd, ptr + total, len - total, 0);
        if(sent < 0)
        {
            if(errno == EINTR) continue;
            return -1;
        }
        total += (size_t)sent;
    }

    return (ssize_t)total;
}

static void _free_response_body(HttpResponse *resp)
{
    if(!resp) return;
    if(resp->body_owned && resp->body)
    {
        free(resp->body);
    }
    resp->body = NULL;
    resp->body_length = 0;
    resp->body_owned = 0;
}

static void _fill_500(HttpResponse *resp)
{
    if(!resp) return;
    resp->status_code = 500;
    resp->status_text = "Internal Server Error";
    resp->content_type = "text/html";
    resp->body = "<html><body><h1>500 Internal Server Error</h1></body></html>";
    resp->body_length = strlen(resp->body);
    resp->body_owned = 0;
}
