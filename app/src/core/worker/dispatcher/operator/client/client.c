#define _GNU_SOURCE

#include "worker/dispatcher/operator/client/client.h"

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <emlog.h>

#include "worker/dispatcher/operator/operator.h"
#include "time_helper.h"
#include "worker/dispatcher/operator/client/browser/http_manager.h"

#define LOG_TAG "srv_client"

static int _on_url(llhttp_t *parser, const char *at, size_t length);
static int _on_message_complete(llhttp_t *parser);
static int _on_header_field(llhttp_t *parser, const char *at, size_t length);
static int _on_header_value(llhttp_t *parser, const char *at, size_t length);
static int _on_body(llhttp_t *parser, const char *at, size_t length);
static int _on_method(llhttp_t *parser, const char *at, size_t length);

int client_http_init(client_http_state_t *st)
{
    if(!st) return STATUS_FAILURE;
    memset(st, 0, sizeof(*st));
    llhttp_settings_init(&st->settings);
    st->settings.on_url = _on_url;
    st->settings.on_header_field = _on_header_field;
    st->settings.on_header_value = _on_header_value;
    st->settings.on_message_complete = _on_message_complete;
    st->settings.on_body = _on_body;
    st->settings.on_method = _on_method;
    llhttp_init(&st->parser, HTTP_REQUEST, &st->settings);
    st->parser.data = st;
    st->method = HTTP_METHOD_UNKNOWN;
    memset(&st->req, 0, sizeof(st->req));
    return STATUS_SUCCESS;
}

static int _on_url(llhttp_t *parser, const char *at, size_t length)
{
    client_http_state_t *st = (client_http_state_t *)parser->data;
    if(!st) return 0;
    size_t copy = length;
    if(copy >= sizeof(st->url)) copy = sizeof(st->url) - 1;
    memcpy(st->url, at, copy);
    st->url[copy] = '\0';
    st->method = (http_method_t)parser->method;
    return 0;
}

static int _on_body(llhttp_t *parser, const char *at, size_t length)
{
    (void)at;
    client_http_state_t *st = (client_http_state_t *)parser->data;
    if(!st) return 0;
    st->body_bytes += length;
    return 0;
}

static int _on_message_complete(llhttp_t *parser)
{
    client_http_state_t *st = (client_http_state_t *)parser->data;
    if(!st) return 0;
    /* store last header if pending */
    if(st->current_field[0] && st->req.header_count < HTTP_MAX_HEADERS_IN)
    {
        int i = st->req.header_count++;
        size_t name_len = strnlen(st->current_field, HTTP_MAX_HEADER_NAME_LEN - 1);
        memcpy(st->req.header_names[i], st->current_field, name_len);
        st->req.header_names[i][name_len] = '\0';

        size_t value_len = strnlen(st->current_value, HTTP_MAX_HEADER_VALUE_LEN - 1);
        memcpy(st->req.header_values[i], st->current_value, value_len);
        st->req.header_values[i][value_len] = '\0';
    }

    st->message_complete = 1;
    return 0;
}

static int _on_header_field(llhttp_t *parser, const char *at, size_t length)
{
    client_http_state_t *st = (client_http_state_t *)parser->data;
    if(!st) return 0;

    /* if switching from value to new field, store previous header */
    if(st->current_field[0] && st->in_header_field == 0 &&
       st->req.header_count < HTTP_MAX_HEADERS_IN)
    {
        int i = st->req.header_count++;
        size_t name_len = strnlen(st->current_field, HTTP_MAX_HEADER_NAME_LEN - 1);
        memcpy(st->req.header_names[i], st->current_field, name_len);
        st->req.header_names[i][name_len] = '\0';

        size_t value_len = strnlen(st->current_value, HTTP_MAX_HEADER_VALUE_LEN - 1);
        memcpy(st->req.header_values[i], st->current_value, value_len);
        st->req.header_values[i][value_len] = '\0';
    }

    size_t copy = length;
    if(copy >= sizeof(st->current_field)) copy = sizeof(st->current_field) - 1;
    memcpy(st->current_field, at, copy);
    st->current_field[copy] = '\0';
    st->in_header_field = 1;
    return 0;
}

static int _on_header_value(llhttp_t *parser, const char *at, size_t length)
{
    client_http_state_t *st = (client_http_state_t *)parser->data;
    if(!st) return 0;
    size_t copy = length;
    if(copy >= sizeof(st->current_value)) copy = sizeof(st->current_value) - 1;
    memcpy(st->current_value, at, copy);
    st->current_value[copy] = '\0';
    st->in_header_field = 0;
    return 0;
}

static int _on_method(llhttp_t *parser, const char *at, size_t length)
{
    (void)at;
    (void)length;
    client_http_state_t *st = (client_http_state_t *)parser->data;
    if(!st) return 0;
    st->method = (http_method_t)parser->method;
    return 0;
}

int client_handle(worker_operator_t *op, worker_client_slot_t *slot)
{
    (void)op;
    if(!slot) return STATUS_FAILURE;

    char buf[HTTP_RECEIVE_BUFFER_LEN];

    for(;;)
    {
        ssize_t n = recv(slot->fd, buf, sizeof(buf), 0);
        if(n > 0)
        {
#ifdef DEBUG_MODE
            EML_DBG(LOG_TAG, "fd %d received %zd bytes", slot->fd, n);
#endif
            slot->last_activity = (uint32_t)time_helper_get_now();
            slot->request_count++;
            enum llhttp_errno err = llhttp_execute(&slot->http.parser, buf, (size_t)n);
            if(err != HPE_OK)
            {
                EML_ERROR(LOG_TAG, "llhttp error on fd %d: %s (%s)",
                          slot->fd,
                          llhttp_errno_name(err),
                          llhttp_get_error_reason(&slot->http.parser));
                return STATUS_FAILURE;
            }

            if(slot->http.message_complete)
            {
                const char *m = http_method_to_string((http_method_t)slot->http.parser.method);
                EML_INFO(LOG_TAG, "fd %d HTTP %s %s body_bytes=%" PRIu64,
                         slot->fd,
                         m ? m : "UNKNOWN",
                         slot->http.url,
                         slot->http.body_bytes);

#ifdef DEBUG_MODE
                EML_DBG(LOG_TAG, "fd %d headers (%d):", slot->fd, slot->http.req.header_count);
                for(int i = 0; i < slot->http.req.header_count; ++i)
                {
                    EML_DBG(LOG_TAG, "  %s: %s",
                            slot->http.req.header_names[i],
                            slot->http.req.header_values[i]);
                }
#endif
                return STATUS_FAILURE; /* close after one request for now */
            }
            continue;
        }
        else if(n == 0)
        {
            /* peer closed */
            return STATUS_FAILURE;
        }
        else
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            {
                return STATUS_SUCCESS;
            }
            EML_PERR(LOG_TAG, "recv failed on fd %d", slot->fd);
            return STATUS_FAILURE;
        }
    }
}
