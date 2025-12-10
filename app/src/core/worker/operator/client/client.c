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
#define _GNU_SOURCE

#include "client.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>

#include "emlog.h"

#include "http_manager.h"
#include "operator.h"
#include "router.h"
#include "time_helper.h"

#define LOG_TAG "srv_client"

static void _populate_transport_meta(operator_t *op, worker_client_slot_t *slot);
static int _send_response(int fd, const Http_request_t *req, const HttpResponse *resp);
static ssize_t _send_all(int fd, const void *buf, size_t len);
static void _free_response_body(HttpResponse *resp);
static void _fill_500(HttpResponse *resp);

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
 * @param op   Owning operator (unused today; placeholder for future routing).
 * @param slot Client slot containing parser state and buffers.
 * @return STATUS_SUCCESS to keep connection; STATUS_FAILURE to drop it.
 */
int client_handle(operator_t *op, worker_client_slot_t *slot)
{
    (void)op;
    if(!slot) return STATUS_FAILURE;

    for(;;)
    {
        ssize_t n = recv(slot->fd, slot->recv_buf, sizeof(slot->recv_buf), 0);
        if(n > 0)
        {
#ifdef DEBUG_MODE
            EML_DBG(LOG_TAG, "fd %d received %zd bytes, executing http parser", slot->fd, n);
#endif
            slot->last_activity = (uint32_t)time_helper_get_now();
            slot->request_count++;
            if(http_parser_execute(&slot->http, slot->recv_buf, (size_t)n) != STATUS_SUCCESS)
            {
                EML_ERROR(LOG_TAG, "HTTP parse failed on fd %d", slot->fd);
                return STATUS_FAILURE;
            }

            if(slot->http.req.message_complete)
            {
#ifdef DEBUG_MODE
                EML_DBG(LOG_TAG, "fd %d HTTP %s %s body_len=%zu",
                         slot->fd,
                         http_method_to_string(slot->http.req.method),
                         slot->http.req.path,
                         slot->http.req.body_len);
                EML_DBG(LOG_TAG, "fd %d headers (%d):", slot->fd, slot->http.req.header_count);
                for(int i = 0; i < slot->http.req.header_count; ++i)
                {
                    EML_DBG(LOG_TAG, "  %s: %s",
                            slot->http.req.header_names[i],
                            slot->http.req.header_values[i]);
                }
#endif

                HttpResponse response = {0};

                _populate_transport_meta(op, slot);

                int route_rc = router_handle_request(&slot->http.req, &response);
                if(route_rc != STATUS_SUCCESS && response.status_code == 0)
                {
                    EML_ERROR(LOG_TAG, "router_handle_request failed for fd %d", slot->fd);
                    _fill_500(&response);
                }

                if(_send_response(slot->fd, &slot->http.req, &response) != STATUS_SUCCESS)
                {
                    _free_response_body(&response);
                    return STATUS_FAILURE;
                }

                _free_response_body(&response);
                http_parser_reset(&slot->http);
                if(slot->http.req.connection_policy == HTTP_CONNECTION_CLOSE)
                {
                    return STATUS_FAILURE; /* close after reply */
                }
                return STATUS_SUCCESS; /* keep alive */
            }
            continue;
        }
        else if(n == 0)
        {
#ifdef DEBUG_MODE
            EML_DBG(LOG_TAG, "fd %d peer closed connection", slot->fd);
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
}

static void _populate_transport_meta(operator_t *op, worker_client_slot_t *slot)
{
    if(!op || !slot) return;

    Http_request_t *req = &slot->http.req;
    req->thread_id = (uint8_t)op->id;

    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    if(getpeername(slot->fd, (struct sockaddr *)&ss, &slen) == 0 && ss.ss_family == AF_INET)
    {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)&ss;
        req->remote_ip_be = sin->sin_addr.s_addr;
        req->remote_port_be = sin->sin_port;
    }
    else
    {
        req->remote_ip_be = 0;
        req->remote_port_be = 0;
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
