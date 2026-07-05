/**
 * @file response_writer.c
 *
 * @brief Response serialization into the client buffer + bounded send loop.
 *
 * S2 note on the send path: partial writes and EAGAIN are handled with a bounded poll(POLLOUT) wait inside the operator thread. The full
 * EPOLLOUT re-arm (parking the unsent tail and resuming from the reactor) is the §9.2 end state and lands with the streaming slice — for
 * API-sized responses on localhost-proxied connections the bounded wait is correct and loud on timeout.
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
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <emlog.h>

#include <db_server/core/worker/operator/client/response_writer.h>

/*****************************************************************************************************************************************
 * PRIVATE DEFINES
 *****************************************************************************************************************************************
 */
#define LOG_TAG                  "srv_response"

/** @brief Max milliseconds to wait for POLLOUT before dropping the client. */
#define RESPONSE_SEND_TIMEOUT_MS 5000

/*****************************************************************************************************************************************
 * PRIVATE ENUMERATED TYPEDEFS
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * PRIVATE STRUCTURED TYPEDEFS
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * PRIVATE VARIABLES
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 *****************************************************************************************************************************************
 */

static const char* _reason_phrase(uint16_t status);
static int         _send_all(client_t* cli, const char* data, size_t len);

/*****************************************************************************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

int response_writer_send(client_t* cli, const DB_app_response_t* res)
{
    if(!cli || cli->ctx.fd < 0 || !res || res->status < 100u || res->status > 599u)
    {
        EML_ERROR(LOG_TAG, "send: invalid arguments");
        return STATUS_FAILURE;
    }

    const int keep_alive = (cli->connection_policy == (uint8_t)HTTP_CONNECTION_KEEP_ALIVE);

    /* Serialize into the client buffer (request views are dead by contract). */
    char*  buf = cli->buf;
    size_t cap = sizeof(cli->buf);
    size_t off = 0u;

    int n = snprintf(buf + off, cap - off, "HTTP/1.1 %u %s\r\nContent-Length: %zu\r\nConnection: %s\r\n", (unsigned)res->status,
                     _reason_phrase(res->status), res->body_len, keep_alive ? "keep-alive" : "close");
    if(n < 0 || (size_t)n >= cap - off)
    {
        goto too_large;
    }
    off += (size_t)n;

    if(res->body_len > 0u && res->content_type)
    {
        n = snprintf(buf + off, cap - off, "Content-Type: %s\r\n", res->content_type);
        if(n < 0 || (size_t)n >= cap - off)
        {
            goto too_large;
        }
        off += (size_t)n;
    }

    for(uint8_t i = 0u; i < res->header_count && i < DB_APP_RESPONSE_HEADERS_MAX; i++)
    {
        n = snprintf(buf + off, cap - off, "%s: %s\r\n", res->headers[i].name, res->headers[i].value);
        if(n < 0 || (size_t)n >= cap - off)
        {
            goto too_large;
        }
        off += (size_t)n;
    }

    if(cap - off < 2u + res->body_len)
    {
        goto too_large;
    }
    buf[off++] = '\r';
    buf[off++] = '\n';
    if(res->body_len > 0u)
    {
        memcpy(buf + off, res->body, res->body_len);
        off += res->body_len;
    }

    return _send_all(cli, buf, off);

too_large:
    EML_ERROR(LOG_TAG, "fd %d: response (status=%u body=%zu headers=%u) exceeds %zu B buffer — sending 500", cli->ctx.fd,
              (unsigned)res->status, res->body_len, (unsigned)res->header_count, cap);
    return response_writer_error(cli, 500u);
}

int response_writer_error(client_t* cli, uint16_t status)
{
    if(!cli || cli->ctx.fd < 0)
    {
        EML_ERROR(LOG_TAG, "error: invalid client");
        return STATUS_FAILURE;
    }

    /* Deliberate errors always close: the connection state is suspect. */
    static const char body_500[] = "{\"error\":\"server_error\"}";
    static const char body_404[] = "{\"error\":\"not_found\"}";
    static const char body_400[] = "{\"error\":\"bad_body\"}";
    const char*       body       = body_500;
    switch(status)
    {
        case 404u:
            body = body_404;
            break;
        case 400u:
            body = body_400;
            break;
        default:
            status = 500u;
            break;
    }
    const size_t body_len = strlen(body);

    char head[256];
    int  n = snprintf(head, sizeof(head),
                      "HTTP/1.1 %u %s\r\nContent-Length: %zu\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n%s",
                      (unsigned)status, _reason_phrase(status), body_len, body);
    if(n < 0 || (size_t)n >= sizeof(head))
    {
        EML_CRIT(LOG_TAG, "error: static error response over 256 B — impossible");
        return STATUS_FAILURE;
    }

    (void)_send_all(cli, head, (size_t)n);
    /* Regardless of send outcome the connection must drop. */
    return STATUS_FAILURE;
}

/*****************************************************************************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

/**
 * @brief Send every byte, tolerating partial writes and bounded EAGAIN waits.
 */
static int _send_all(client_t* cli, const char* data, size_t len)
{
    size_t sent = 0u;
    while(sent < len)
    {
        ssize_t n = write(cli->ctx.fd, data + sent, len - sent);
        if(n > 0)
        {
            sent += (size_t)n;
            continue;
        }
        if(n < 0 && errno == EINTR)
        {
            continue;
        }
        if(n < 0 && errno == EAGAIN)
        {
            struct pollfd pfd = {.fd = cli->ctx.fd, .events = POLLOUT};
            int           pr  = poll(&pfd, 1, RESPONSE_SEND_TIMEOUT_MS);
            if(pr == 1 && (pfd.revents & POLLOUT))
            {
                continue;
            }
            EML_ERROR(LOG_TAG, "fd %d: POLLOUT wait failed (pr=%d revents=0x%x) after %zu/%zu B", cli->ctx.fd, pr, (unsigned)pfd.revents,
                      sent, len);
            return STATUS_FAILURE;
        }
        EML_PERR(LOG_TAG, "fd %d: write failed after %zu/%zu B", cli->ctx.fd, sent, len);
        return STATUS_FAILURE;
    }
    return STATUS_SUCCESS;
}

/**
 * @brief Reason phrase for the status codes the platform actually emits.
 */
static const char* _reason_phrase(uint16_t status)
{
    switch(status)
    {
        case 200u:
            return "OK";
        case 201u:
            return "Created";
        case 400u:
            return "Bad Request";
        case 401u:
            return "Unauthorized";
        case 403u:
            return "Forbidden";
        case 404u:
            return "Not Found";
        case 409u:
            return "Conflict";
        case 413u:
            return "Content Too Large";
        case 500u:
            return "Internal Server Error";
        default:
            return "Status";
    }
}
