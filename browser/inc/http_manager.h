#ifndef HTTP_MANAGER_H
#define HTTP_MANAGER_H


/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */
#include "server_settings.h"
#include <stddef.h>                 // size_t


/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */
/* None */


/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DEFINITIONS
 ****************************************************************************
 */

typedef struct {
    char method[HTTP_MAX_METHOD_LEN];
    char path[HTTP_MAX_PATH_LEN];
    char header_names[HTTP_MAX_HEADER_COUNT][HTTP_MAX_HEADER_NAME_LEN];
    char header_values[HTTP_MAX_HEADER_COUNT][HTTP_MAX_HEADER_VALUE_LEN];
    int header_count;
} HttpRequest;

typedef struct {
    int status_code;
    const char* status_text;
    const char* content_type;
    const char* body;
    size_t body_length;
} HttpResponse;

/* Struct to keep track of actual llhttp parsing state */
typedef struct {
    HttpRequest* req;
    char current_field[HTTP_MAX_HEADER_NAME_LEN];
    char current_value[HTTP_MAX_HEADER_VALUE_LEN];
    int in_header_field;
} LlhttpParserContext;


/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/* Parse an HTTP request from a buffer */
int http_parse_request(const char* buffer, size_t n, HttpRequest* req);

/* Build an HTTP response string into a buffer (already formatted) */
int http_build_response(const HttpResponse* resp, char* out_buffer, size_t max_len);



#endif /* HTTP_MANAGER_H */
