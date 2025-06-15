#ifndef HTTP_MANAGER_H
#define HTTP_MANAGER_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */
#include <stddef.h>  // size_t
#include <unistd.h>  // ssize_t

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
 * @brief Supported HTTP methods.
 */
typedef enum
{
    HTTP_METHOD_GET,    /* GET method */
    HTTP_METHOD_POST,   /* POST method */
    HTTP_METHOD_PUT,    /* PUT method */
    HTTP_METHOD_DELETE, /* DELETE method */
    HTTP_METHOD_UNKNOWN /* Unknown method */
} http_method_t;

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
    char header_names[HTTP_MAX_HEADER_COUNT][HTTP_MAX_HEADER_NAME_LEN];   /* Header names */
    char header_values[HTTP_MAX_HEADER_COUNT][HTTP_MAX_HEADER_VALUE_LEN]; /* Header values */
    int header_count;                       /* Number of headers parsed */
    HTTPConnectionPolicy connection_policy; /* Connection policy (keep-alive or close) */

} HttpRequest;

/**
 * @brief HTTP response to be sent to the client.
 */
typedef struct
{
    int status_code;
    const char* status_text;
    const char* content_type;
    const char* body;
    size_t body_length;
} HttpResponse;

/**
 * @brief Struct to keep track of actual llhttp parsing state
 */
typedef struct
{
    HttpRequest* req;
    char current_field[HTTP_MAX_HEADER_NAME_LEN];
    char current_value[HTTP_MAX_HEADER_VALUE_LEN];
    int in_header_field;
} LlhttpParserContext;

/****************************************************************************
 * PUBLIC ENUMERATED VARIABLES
 ****************************************************************************
 */

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

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
 * @brief Parse a raw HTTP request buffer into a structured HttpRequest.
 *
 * Uses llhttp to parse the HTTP request line, headers, and path.
 * Populates the HttpRequest struct with method, path, and headers.
 * Determines the connection policy (keep-alive/close).
 *
 * @param buffer        Pointer to the raw HTTP request buffer.
 * @param buffer_len    Length of the buffer.
 * @param req           Pointer to the HttpRequest struct to populate.
 * @retval  0  Success.
 * @retval -1  Parse error (malformed request).
 */
int http_parse_request(const char* buffer, size_t buffer_len, HttpRequest* req);

#endif /* HTTP_MANAGER_H */
