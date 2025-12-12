/**
 * @file http_manager.h
 * @brief Minimal HTTP/1.x parsing and response helpers for operator client path.
 */
#ifndef SERVER_HTTP_MANAGER_H
#define SERVER_HTTP_MANAGER_H

/****************************************************************************
 * INCLUDES
 ****************************************************************************
 */
#include <string.h>

#include "http_common.h"
#include "llhttp.h"

/****************************************************************************
 * DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * ENUMERATED TYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * ENUMERATED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * STRUCTURED TYPES
 ****************************************************************************
 */

/**
 * @brief Struct to keep track of actual llhttp parsing state
 */
typedef struct
{
    http_request_t req;   /* populated request */

    sv_t current_field;   /* header field being accumulated */
    sv_t current_value;   /* header value being accumulated */
    sv_t current_url;     /* request-target (path+query etc.) */
    sv_t current_body;    /* body sl      ice */

    int  in_header_field; /* 1 if we are currently appending to field, 0 for value */

    uint8_t parsing;      /* 1 if parsing is ongoing */
    size_t buf_used;      /* bytes currently stored in cli->recv_buf for this message */
} llhttp_parser_ctx_t;

typedef struct
{
    llhttp_t parser;                    /* llhttp parser instance */
    llhttp_settings_t parser_settings;  /* llhttp settings/callbacks */
    llhttp_parser_ctx_t parser_ctx;     /* populated request */
} llhttp_parser_t;

/**
 * @brief HTTP response to be sent to the client.
 */
typedef struct
{
    int status_code;
    const char* status_text;
    const char* content_type;
    char* body;
    size_t body_length;
    uint8_t body_owned; /* 1 if body must be free()'d by caller */

    /* generic headers (e.g., Set-Cookie) */
    char header_names[HTTP_MAX_HEADERS_OUT][HTTP_MAX_HEADER_NAME_LEN];
    char header_values[HTTP_MAX_HEADERS_OUT][HTTP_MAX_HEADER_VALUE_LEN];
    int header_count;
} HttpResponse;

/****************************************************************************
 * ENUMERATED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

static inline int http_response_add_header(HttpResponse* r, const char* name, const char* val)
{
    if(!r || !name || !val) return -1;
    if(r->header_count >= (int)HTTP_MAX_HEADERS_OUT) return -1;
    int i = r->header_count++;
    strncpy(r->header_names[i], name, HTTP_MAX_HEADER_NAME_LEN - 1);
    r->header_names[i][HTTP_MAX_HEADER_NAME_LEN - 1] = 0;
    strncpy(r->header_values[i], val, HTTP_MAX_HEADER_VALUE_LEN - 1);
    r->header_values[i][HTTP_MAX_HEADER_VALUE_LEN - 1] = 0;
    return 0;
}

static inline const char* http_method_to_string(http_method_t method)
{
    switch(method)
    {
        case HTTP_METHOD_GET:
            return "GET";
        case HTTP_METHOD_POST:
            return "POST";
        case HTTP_METHOD_PUT:
            return "PUT";
        case HTTP_METHOD_DELETE:
            return "DELETE";
        default:
            return "UNKNOWN";
    }
}

static inline http_method_t http_method_from_llhttp_method(llhttp_method_t m)
{
    switch (m)
    {
        case HTTP_GET:    return HTTP_METHOD_GET;
        case HTTP_POST:   return HTTP_METHOD_POST;
        case HTTP_PUT:    return HTTP_METHOD_PUT;
        case HTTP_DELETE: return HTTP_METHOD_DELETE;
        default:          return HTTP_METHOD_UNKNOWN;
    }
}

/**
 * @brief Initialize a streaming HTTP parser state.
 */
int http_man_init(llhttp_parser_t *pstate);

/**
 * @brief Feed data into the streaming HTTP parser.
 *
 * @return STATUS_SUCCESS on success, STATUS_FAILURE on parse error.
 *         When a full message is parsed, pstate->req is populated.
 */
int http_man_execute(llhttp_parser_t *pstate, const char *buf, size_t len);

/**
 * @brief Reset parser state for a new message.
 */
void http_man_reset(llhttp_parser_t *pstate);

#endif /* SERVER_HTTP_MANAGER_H */
