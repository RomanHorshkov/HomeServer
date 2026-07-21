/**
 * @file response_writer.c
 *
 * @brief Response serialization into the client buffer + send, with EPOLLOUT parking (§9.2 end state).
 *
 * A response is always fully serialized into cli->buf first. Sending it either finishes inside the same
 * call, or — when the client's fd is reactor-managed and the kernel can't take the whole thing right now
 * — parks: cli->draining is set, the unsent tail stays in cli->buf, the fd is re-armed for EPOLLOUT via
 * the operator's own reactor, and control returns to the caller without blocking the operator thread.
 * response_writer_resume() is what the reactor calls back into once EPOLLOUT actually fires.
 *
 * Close-vs-keep-alive is a DEFERRED decision everywhere a send might park: it is never made at the point
 * a response is handed off, only once the send is known to be fully complete (either synchronously here,
 * or later in response_writer_resume()). client_handle() honors this via cli->draining before it ever
 * looks at cli->connection_policy.
 *
 * The upload-worker pool drives its clients with its own blocking poll() loop, not a reactor (cli->ctx.owner
 * is NULL for those clients — nothing to park against, and blocking there is the documented design:
 * upload_worker.c "Blocking the worker in the pump is fine — that is its whole job"). _try_send() falls
 * back to the original bounded poll(POLLOUT) wait in that case, unchanged from before this file's rewrite.
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
#include <sys/epoll.h>
#include <unistd.h>

#include <emlog.h>

#include <db_server/core/reactor.h>
#include <db_server/core/worker/operator/client/response_writer.h>
#include <db_server/core/worker/operator/operator.h>
#include <db_server/utils/time_helper.h>

/*****************************************************************************************************************************************
 * PRIVATE DEFINES
 *****************************************************************************************************************************************
 */
#define LOG_TAG                  "srv_response"

/** @brief Max milliseconds to wait for POLLOUT before dropping a NON-reactor-managed client (the
 *         upload-worker pool's blocking fallback only — reactor-managed clients never block here). */
#define RESPONSE_SEND_TIMEOUT_MS 5000

/** @brief Event mask for a client fd while its response is fully drained and it's ready for the next
 *         request — mirrors reactor_add_in_client()'s own mask exactly. */
#define CLIENT_EVENTS_IN         (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)

/** @brief Event mask for a client fd while a response is parked, waiting to finish sending. */
#define CLIENT_EVENTS_OUT        (EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR)

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
static int         _park(client_t* cli, size_t total_len);
static int         _try_send(client_t* cli, size_t total_len);

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

    cli->send_off = 0u;
    return _try_send(cli, off);

too_large:
    EML_ERROR(LOG_TAG, "fd %d: response (status=%u body=%zu headers=%u) exceeds %zu B buffer — sending 500", cli->ctx.fd,
              (unsigned)res->status, res->body_len, (unsigned)res->header_count, cap);
    return response_writer_error(cli, 500u);
}

int response_writer_resume(client_t* cli)
{
    if(!cli || cli->ctx.fd < 0 || !cli->draining)
    {
        EML_ERROR(LOG_TAG, "resume: invalid state (fd=%d draining=%d)", cli ? cli->ctx.fd : -1, cli ? (int)cli->draining : -1);
        return STATUS_FAILURE;
    }

    const size_t total_len = cli->send_len;
    if(_try_send(cli, total_len) != STATUS_SUCCESS)
    {
        return STATUS_FAILURE; /* real write error — caller (operator.c) removes the client */
    }
    if(cli->draining)
    {
        return STATUS_SUCCESS; /* EAGAIN again — still parked, reactor stays armed for EPOLLOUT */
    }

    /* Fully sent. buf is free again — re-arm EPOLLIN for the next request BEFORE making the
     * close-vs-keep-alive call this exchange deferred while draining. */
    operator_t* op = (operator_t*)cli->ctx.owner;
    if(!op || reactor_mod(&op->reactor, cli->ctx.fd, CLIENT_EVENTS_IN, &cli->ctx) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "fd %d: reactor_mod back to EPOLLIN failed — dropping", cli->ctx.fd);
        return STATUS_FAILURE;
    }
    if(cli->connection_policy == (uint8_t)HTTP_CONNECTION_CLOSE)
    {
        return STATUS_FAILURE; /* deliberate close, not an error — caller removes the client */
    }
    return STATUS_SUCCESS;
}

int response_writer_error(client_t* cli, uint16_t status)
{
    if(!cli || cli->ctx.fd < 0)
    {
        EML_ERROR(LOG_TAG, "error: invalid client");
        return STATUS_FAILURE;
    }

    /* Deliberate errors always close: the connection state is suspect. Set the policy BEFORE sending
     * so both the Connection header this response carries and the eventual close decision (immediate
     * if this send completes synchronously below, deferred to response_writer_resume() if it parks)
     * agree — there is exactly one place that decides "close", not two. */
    cli->connection_policy = (uint8_t)HTTP_CONNECTION_CLOSE;

    static const char body_500[] = "{\"error\":\"server_error\"}";
    static const char body_404[] = "{\"error\":\"not_found\"}";
    static const char body_400[] = "{\"error\":\"bad_body\"}";
    static const char body_413[] = "{\"error\":\"body_too_large\"}";
    const char*       body       = body_500;
    switch(status)
    {
        case 404u:
            body = body_404;
            break;
        case 400u:
            body = body_400;
            break;
        case 413u: /* an over-declared Content-Length the parser rejected before the body (§9.4 upload ceiling) */
            body = body_413;
            break;
        default:
            status = 500u;
            break;
    }
    const size_t body_len = strlen(body);

    /* Built into cli->buf, not a local stack array: if this parks, the bytes must survive past this
     * function returning for response_writer_resume() to finish sending them later. */
    char*        buf = cli->buf;
    const size_t cap = sizeof(cli->buf);
    int          n   = snprintf(buf, cap, "HTTP/1.1 %u %s\r\nContent-Length: %zu\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n%s",
                                (unsigned)status, _reason_phrase(status), body_len, body);
    if(n < 0 || (size_t)n >= cap)
    {
        EML_CRIT(LOG_TAG, "error: static error response over %zu B — impossible", cap);
        return STATUS_FAILURE;
    }

    cli->send_off = 0u;
    return _try_send(cli, (size_t)n);
}

/*****************************************************************************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

/**
 * @brief Register this fd for EPOLLOUT and mark it draining. Called only when a write returned EAGAIN
 *        on a reactor-managed client (cli->ctx.owner is a live operator_t*).
 */
static int _park(client_t* cli, size_t total_len)
{
    operator_t* op = (operator_t*)cli->ctx.owner;
    if(reactor_mod(&op->reactor, cli->ctx.fd, CLIENT_EVENTS_OUT, &cli->ctx) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "fd %d: reactor_mod to EPOLLOUT failed — dropping (%zu/%zu B sent)", cli->ctx.fd, cli->send_off, total_len);
        cli->draining = 0u;
        cli->send_off = 0u;
        cli->send_len = 0u;
        return STATUS_FAILURE;
    }
    cli->draining = 1u;
    cli->send_len = total_len;
    return STATUS_SUCCESS;
}

/**
 * @brief Write cli->buf[cli->send_off, total_len) to the socket, continuing from wherever send_off
 *        already is (0 for a fresh send, mid-way for a resume). Tolerates partial writes and EINTR.
 *
 * @retval STATUS_SUCCESS  every byte reached the kernel (drain state is cleared), OR the client is
 *                         reactor-managed and this parked on EPOLLOUT (draining stays set) — the two
 *                         are told apart by checking cli->draining after this returns.
 * @retval STATUS_FAILURE  a real write error, or (non-reactor-managed clients only) the bounded
 *                         poll(POLLOUT) wait timed out.
 */
static int _try_send(client_t* cli, size_t total_len)
{
    const int reactor_managed = (cli->ctx.owner != NULL);

    while(cli->send_off < total_len)
    {
        ssize_t n = write(cli->ctx.fd, cli->buf + cli->send_off, total_len - cli->send_off);
        if(n > 0)
        {
            cli->send_off     += (size_t)n;
            cli->last_activity = (uint64_t)time_helper_get_now(); /* real progress — not an idle client */
            continue;
        }
        if(n < 0 && errno == EINTR)
        {
            continue;
        }
        if(n < 0 && errno == EAGAIN)
        {
            if(reactor_managed)
            {
                return _park(cli, total_len);
            }
            /* upload-worker path: no reactor to park against — block, exactly as before this rewrite. */
            struct pollfd pfd = {.fd = cli->ctx.fd, .events = POLLOUT};
            int           pr  = poll(&pfd, 1, RESPONSE_SEND_TIMEOUT_MS);
            if(pr == 1 && (pfd.revents & POLLOUT))
            {
                continue;
            }
            EML_ERROR(LOG_TAG, "fd %d: POLLOUT wait failed (pr=%d revents=0x%x) after %zu/%zu B", cli->ctx.fd, pr, (unsigned)pfd.revents,
                      cli->send_off, total_len);
            cli->send_off = 0u;
            return STATUS_FAILURE;
        }
        EML_PERR(LOG_TAG, "fd %d: write failed after %zu/%zu B", cli->ctx.fd, cli->send_off, total_len);
        cli->draining = 0u;
        cli->send_off = 0u;
        cli->send_len = 0u;
        return STATUS_FAILURE;
    }

    /* Fully sent. */
    cli->draining = 0u;
    cli->send_off = 0u;
    cli->send_len = 0u;
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
        case 507u: /* upload over the physical disk headroom (DB_app insufficient_storage) */
            return "Insufficient Storage";
        default:
            return "Status";
    }
}
