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
 * PUBLIC STRUCTURED VARIABLES DEFINITIONS
 ****************************************************************************
 */

typedef struct
{
    char method[HTTP_MAX_METHOD_LEN];
    char path[HTTP_MAX_PATH_LEN];
    char header_names[HTTP_MAX_HEADER_COUNT][HTTP_MAX_HEADER_NAME_LEN];
    char header_values[HTTP_MAX_HEADER_COUNT][HTTP_MAX_HEADER_VALUE_LEN];
    int header_count;
    int should_close;  // 1 = close after response, 0 = keep alive

} HttpRequest;

typedef struct
{
    int status_code;
    const char* status_text;
    const char* content_type;
    const char* body;
    size_t body_length;
} HttpResponse;

/* Struct to keep track of actual llhttp parsing state */
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
enum HTTPConnectionPolicy
{
    HTTP_CONNECTION_KEEP_ALIVE = 0,
    HTTP_CONNECTION_CLOSE
};

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief Parse an HTTP/1.1 request into an HttpRequest struct.
 *
 * The caller owns @p out and must free it with http_request_destroy().
 *
 * @param[in]  buffer   Raw request buffer (NUL‑terminated)
 * @param[out] out      Newly allocated HttpRequest
 * @return              0 on success, negative errno on failure
 */
int http_parse_request(const char* buffer, size_t buffer_len, HttpRequest* req,
                       int* client_connection_policy);

/* Build an HTTP response string into a buffer (already formatted) */
int http_build_response(const HttpResponse* resp, const int* client_connection_policy,
                        char* out_buffer, size_t max_len);

#endif /* HTTP_MANAGER_H */
