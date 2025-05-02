#define _GNU_SOURCE

#include "http_manager.h"
#include "logger.h"
#include "llhttp.h"

#include <errno.h>                  // errno, EADDRINUSE, etc.
#include <string.h>                 // memset(), strcpy(), strlen(), etc.
#include <stdio.h>
#include <ctype.h>






/****************************************************************************
 * PRIVATE FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

static int on_url(llhttp_t* parser, const char* at, size_t length);
static int on_header_field(llhttp_t* parser, const char* at, size_t length);
static int on_header_value(llhttp_t* parser, const char* at, size_t length);
static int on_method(llhttp_t* parser, const char* at, size_t length);
static void determine_connection_policy(HttpRequest* req, int* client_connection_policy);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

/**
 * Parses a basic HTTP request.
 * Fills HttpRequest with method and path if successful.
 */
int http_parse_request(const char* buffer, const size_t buffer_len, HttpRequest* req, int* client_connection_policy)
{
    llhttp_t parser;
    llhttp_settings_t settings;

    llhttp_settings_init(&settings);
    settings.on_url = on_url;
    settings.on_method = on_method;
    settings.on_header_field = on_header_field;
    settings.on_header_value = on_header_value;

    llhttp_init(&parser, HTTP_REQUEST, &settings);

    LlhttpParserContext ctx = {
        .req = req,
        .in_header_field = 1
    };

    parser.data = &ctx;

    memset(req, 0, sizeof(HttpRequest)); // zero fields

    llhttp_errno_t err = llhttp_execute(&parser, buffer, buffer_len);

    // Store last header if not already stored
    if (ctx.current_field[0] && ctx.req->header_count < HTTP_MAX_HEADER_COUNT)
    {
        int i = ctx.req->header_count++;
        // Copy header name
        size_t name_len = strnlen(ctx.current_field, HTTP_MAX_HEADER_NAME_LEN - 1);
        memcpy(ctx.req->header_names[i], ctx.current_field, name_len);
        ctx.req->header_names[i][name_len] = '\0';

        // Copy header value
        size_t value_len = strnlen(ctx.current_value, HTTP_MAX_HEADER_VALUE_LEN - 1);
        memcpy(ctx.req->header_values[i], ctx.current_value, value_len);
        ctx.req->header_values[i][value_len] = '\0';
    }

    /* Determine if the client requested Connection: close */
    determine_connection_policy(req, client_connection_policy);

    if (err != HPE_OK)
    {
        log_error("llhttp parse error", llhttp_errno_name(err));
        return -1;
    }
    else
    {   
        /* log all the html headers */
        log_info("METHOD: %s\n", req->method);
        log_info("PATH: %s\n", req->path);
        for (int i = 0; i < req->header_count; ++i)
        {
            log_info("%s: %s\n", req->header_names[i], req->header_values[i]);
        }
    }

    return 0;
}

/**
 * Builds a simple HTTP response in the output buffer.
 * Fills the out_buffer with the complete HTTP response.
 */
int http_build_response(const HttpResponse* resp, int* client_connection_policy, char* out_buffer, size_t max_len)
{
    /* return value */
    int res = -1;
    
    /* check input validity */
    if (resp != NULL && out_buffer != NULL)
    {
        const char* conn_hdr = (*client_connection_policy == HTTP_CONNECTION_CLOSE ? "close" : "keep-alive");
        int written = snprintf(out_buffer, max_len,
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: %s\r\n"
            "\r\n"
            "%s",
            resp->status_code,
            resp->status_text,
            resp->content_type,
            resp->body_length,
            conn_hdr,
            (resp->body != NULL) ? resp->body : "");
            
        if (written > 0 && (size_t)written < max_len)
        {
            res = written;
        }
        else
        {
            log_error("http_manager: build response: snprintf failed or buffer too small", strerror(errno));
        }
    }
    else
    {
        log_error("http_manager: build response: invalid input", strerror(errno));
    }
    
    return res;
}


/****************************************************************************
 * PRIVATE FUNCTIONS DEINITIONS
 ****************************************************************************
 */

// Called for URL
static int on_url(llhttp_t* parser, const char* at, size_t length)
{
    LlhttpParserContext* ctx = (LlhttpParserContext*) parser->data;
    snprintf(ctx->req->path, HTTP_MAX_PATH_LEN, "%.*s", (int)length, at);
    return 0;
}

/** 
 * Called when header name is parsed 
 */
static int on_header_field(llhttp_t* parser, const char* at, size_t length)
{
    LlhttpParserContext* ctx = (LlhttpParserContext*) parser->data;

    // If were finishing a previous header, store it
    if (!ctx->in_header_field && ctx->req->header_count < HTTP_MAX_HEADER_COUNT)
    {
        int i = ctx->req->header_count++;
        strncpy(ctx->req->header_names[i], ctx->current_field, HTTP_MAX_HEADER_NAME_LEN);
        strncpy(ctx->req->header_values[i], ctx->current_value, HTTP_MAX_HEADER_VALUE_LEN);
        ctx->current_field[0] = 0;
        ctx->current_value[0] = 0;
    }

    snprintf(ctx->current_field, HTTP_MAX_HEADER_NAME_LEN, "%.*s", (int)length, at);
    ctx->in_header_field = 1;
    return 0;
}

/**
 * Called when header value is parsed
 */
static int on_header_value(llhttp_t* parser, const char* at, size_t length)
{
    LlhttpParserContext* ctx = (LlhttpParserContext*) parser->data;
    snprintf(ctx->current_value, HTTP_MAX_HEADER_VALUE_LEN, "%.*s", (int)length, at);
    ctx->in_header_field = 0;
    return 0;
}

/**
 * Called when method is detected
 */
static int on_method(llhttp_t* parser, const char* at, size_t length)
{
    LlhttpParserContext* ctx = (LlhttpParserContext*) parser->data;
    snprintf(ctx->req->method, HTTP_MAX_METHOD_LEN, "%.*s", (int)length, at);
    return 0;
}

/**
 * Analyze headers in an HttpRequest to determine if the client wants to close the connection.
 * Sets req->should_close accordingly.
 */
static void determine_connection_policy(HttpRequest* req, int* client_connection_policy)
{
    req->should_close = 0; // Default for HTTP/1.1 is keep-alive
    *client_connection_policy = HTTP_CONNECTION_KEEP_ALIVE;

    for (int i = 0; i < req->header_count; ++i)
    {
        if (strcasecmp(req->header_names[i], "Connection") == 0 &&
            strcasestr(req->header_values[i], "close"))
        {
            req->should_close = 1;
            *client_connection_policy = HTTP_CONNECTION_CLOSE;
            break;
        }
    }
}
