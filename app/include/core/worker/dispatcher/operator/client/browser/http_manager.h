/**
 * @file http_manager.h
 * @brief Minimal HTTP/1.x parsing and response helpers for operator client path.
 */
#ifndef HTTP_MANAGER_H
#define HTTP_MANAGER_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */
#include <stddef.h>  // size_t
#include <stdint.h>
#include <unistd.h>  // ssize_t
#include <llhttp.h>

#include <string.h>

#include "server_settings.h"

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC ENUMERATED VARIABLES
 ****************************************************************************
 */

/**
 * @brief Connection policy for HTTP/1.x.
 */
typedef enum
{
    HTTP_CONNECTION_KEEP_ALIVE = 0, /* Keep connection alive */
    HTTP_CONNECTION_CLOSE = 1,      /* Close connection */
} HTTPConnectionPolicy;

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DEFINITIONS
 ****************************************************************************
 */

/**
 * @brief Parsed HTTP request.
 */
typedef struct
{
    http_method_t method;         /* HTTP method (GET, POST, etc.) */
    char path[HTTP_MAX_PATH_LEN]; /* Request path */
    uint32_t remote_ip_be;        /* peer IPv4 (network order), optional */
    uint16_t remote_port_be;      /* peer port (network order), optional */
    uint8_t  thread_id;           /* operator thread id carrying the request */
    char header_names[HTTP_MAX_HEADERS_IN][HTTP_MAX_HEADER_NAME_LEN];   /* Header names */
    char header_values[HTTP_MAX_HEADERS_IN][HTTP_MAX_HEADER_VALUE_LEN]; /* Header values */
    int header_count;                       /* Number of headers parsed */
    char body[HTTP_MAX_BODY_RAM_CAPACITY];  /* Preallocated request body buffer */
    size_t body_len;                        /* Length of the body in bytes */
    HTTPConnectionPolicy connection_policy; /* Connection policy (keep-alive or close) */
    int message_complete;                   /* set when llhttp signals completion */

} HttpRequest;

typedef struct http_parser
{
    llhttp_t parser;
    llhttp_settings_t settings;
    HttpRequest req;
} http_parser_t;

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
 * PUBLIC ENUMERATED VARIABLES
 ****************************************************************************
 */

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

static inline int http_response_add_header(HttpResponse* r, const char* name, const char* val)
{
    if(!r || !name || !val) return -1;
    if(r->header_count >= HTTP_MAX_HEADERS_OUT) return -1;
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

/**
 * @brief Parse a raw HTTP request buffer into a structured HttpRequest
 * and sanitize it.
 *
 * Uses llhttp to parse the HTTP request line, headers, and path.
 * Populates the HttpRequest struct with method, path, and headers.
 * Determines the connection policy (keep-alive/close).
 * Sanitizes the request for malformed headers.
 *
 * @param buffer        Pointer to the raw HTTP request buffer.
 * @param buffer_len    Length of the buffer.
 * @param req           Pointer to the HttpRequest struct to populate.
 * @retval  0  Success.
 * @retval -1  Parse error (malformed request).
 */
int http_manage_request(const char* recv_buf, const size_t buffer_len, HttpRequest* request);

/**
 * @brief Initialize a streaming HTTP parser state.
 */
int http_parser_init(http_parser_t *pstate);

/**
 * @brief Feed data into the streaming HTTP parser.
 *
 * @return STATUS_SUCCESS on success, STATUS_FAILURE on parse error.
 *         When a full message is parsed, pstate->req is populated.
 */
int http_parser_execute(http_parser_t *pstate, const char *buf, size_t len);

/**
 * @brief Reset parser state for a new message.
 */
void http_parser_reset(http_parser_t *pstate);

#endif /* HTTP_MANAGER_H */
