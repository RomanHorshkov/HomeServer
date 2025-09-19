/**
 * @file http_manager.c
 * @brief Parses and manages raw HTTP requests.
 *
 * Uses llhttp to extract method, path, headers, and body from incoming data.
 * Enforces basic security checks (no path traversal, safe headers) and determines
 * connection policy (keep-alive or close) before returning a structured HttpRequest.
 *
 * Usage:
 *   http_manage_request(buffer, length, request);
 *
 * Exit Codes:
 *   STATUS_SUCCESS  (0)
 *   STATUS_FAILURE  (1)
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑05‑11
 * (c) 2025
 */

#define _GNU_SOURCE
#include "http_manager.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>  // malloc(), free()

#include "llhttp.h"
#include "logger.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE ENUMERATED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */

/**
 * @brief Struct to keep track of actual llhttp parsing state
 */
typedef struct
{
    HttpRequest* req;
    char         current_field[HTTP_MAX_HEADER_NAME_LEN];
    char         current_value[HTTP_MAX_HEADER_VALUE_LEN];
    int          in_header_field;

} LlhttpParserContext;

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief llhttp callback: called when the URL is parsed.
 *
 * Copies the parsed URL/path into the request structure.
 *
 * @param parser   Pointer to the llhttp parser.
 * @param at       Pointer to the start of the URL string.
 * @param length   Length of the URL string.
 * @return         0 on success.
 */
static int on_url(llhttp_t* parser, const char* at, size_t length);

/**
 * @brief llhttp callback: called when a header field is parsed.
 *
 * Stores the header field name in the parser context. If a previous header
 * was being parsed, stores the previous header name and value in the request.
 *
 * @param parser   Pointer to the llhttp parser.
 * @param at       Pointer to the start of the header field string.
 * @param length   Length of the header field string.
 * @return         0 on success.
 */
static int on_header_field(llhttp_t* parser, const char* at, size_t length);

/**
 * @brief llhttp callback: called when a header value is parsed.
 *
 * Stores the header value in the parser context.
 *
 * @param parser   Pointer to the llhttp parser.
 * @param at       Pointer to the start of the header value string.
 * @param length   Length of the header value string.
 * @return         0 on success.
 */
static int on_header_value(llhttp_t* parser, const char* at, size_t length);

static int on_body(llhttp_t* parser, const char* at, size_t length);

/**
 * @brief llhttp callback: called when the HTTP method is parsed.
 *
 * Converts the parsed method string to an enum and stores it in the request.
 *
 * @param parser   Pointer to the llhttp parser.
 * @param at       Pointer to the start of the method string.
 * @param length   Length of the method string.
 * @return         0 on success.
 */
static int on_method(llhttp_t* parser, const char* at, size_t length);

/**
 * @brief Converts a method string to the http_method_t enum.
 *
 * @param at       Pointer to the method string.
 * @param length   Length of the method string.
 * @return         Corresponding http_method_t value.
 */
static http_method_t parse_http_method(const char* at, size_t length);

/**
 * @brief Parses a raw HTTP request buffer into an HttpRequest structure.
 *
 * This function processes the provided HTTP request buffer, extracting the HTTP method,
 * path, headers, and other relevant information, and populates the given HttpRequest struct.
 *
 * @param buffer      Pointer to the raw HTTP request buffer.
 * @param buffer_len  Length of the buffer in bytes.
 * @param req         Pointer to the HttpRequest structure to populate.
 * @return            0 on success, negative value on failure.
 */
static int http_parse_request(const char* buffer, const size_t buffer_len,
                              HttpRequest* req);

/**
 * @brief Validates and sanitizes a parsed HttpRequest structure.
 *
 * This function checks the HttpRequest for security and protocol compliance, including:
 *   - Non-empty and safe path (no traversal, no control chars)
 *   - Valid HTTP method
 *   - Header count and length limits
 *   - Safe header names and values (no control chars, no nulls)
 *   - Logs any issues found
 *
 * @param req         Pointer to the parsed HttpRequest structure to validate.
 * @return            STATUS_SUCCESS if valid, STATUS_FAILURE otherwise.
 */
static int sanitize_http_request(HttpRequest* req);

/**
 * @brief Determines the connection policy (keep-alive or close) from headers.
 *
 * Scans the parsed headers for "Connection: close" and sets the policy in the request.
 *
 * @param req      Pointer to the HttpRequest structure to update.
 */
static void determine_connection_policy(HttpRequest* req);

/**
 * @brief Validates the HTTP request path for security and protocol compliance.
 * Checks for empty, traversal, control chars, and suspicious chars.
 * @return STATUS_SUCCESS if valid, STATUS_FAILURE otherwise.
 */
static int validate_http_path(const char* path);

/**
 * @brief Validates a header name for length and safe characters.
 * @return STATUS_SUCCESS if valid, STATUS_FAILURE otherwise.
 */
static int validate_http_header_name(const char* hname);

/**
 * @brief Validates a header value for length and safe characters.
 * @return STATUS_SUCCESS if valid, STATUS_FAILURE otherwise.
 */
static int validate_http_header_value(const char* hval, const char* hname);

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int http_manage_request(const char* recv_buf, const size_t buffer_len,
                        HttpRequest* request)
{
    /* result variable */
    int res = STATUS_FAILURE;

    /* Allocate memory and parse raw HTTP request into HttpRequest struct */
    if(http_parse_request(recv_buf, buffer_len, request) != STATUS_SUCCESS)
    {
        log_error("[browser] parse failed", strerror(errno));
    }

    /* Sanitize parsed HTTP reuest */
    else if(sanitize_http_request(request) != STATUS_SUCCESS)
    {
        log_error("[browser] sanitize_http_request failed", strerror(errno));
    }

    else
    {
        /* Set return variable to success */
        res = STATUS_SUCCESS;

        /* Determine connection policy based on headers */
        determine_connection_policy(request);
    }

    return res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEINITIONS
 ****************************************************************************
 */

static int http_parse_request(const char* buffer, const size_t buffer_len,
                              HttpRequest* req)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* llhttp parser declare */
    llhttp_t          parser;
    llhttp_settings_t settings;

    /* llhttp parser initialize */
    llhttp_settings_init(&settings);

    settings.on_url          = on_url;
    settings.on_method       = on_method;
    settings.on_header_field = on_header_field;
    settings.on_header_value = on_header_value;
    settings.on_body         = on_body;

    llhttp_init(&parser, HTTP_REQUEST, &settings);

    /* Create parsing context to keep track of parsing */
    LlhttpParserContext ctx = {.req = req, .in_header_field = 1};

    /* Assign the context to parser */
    parser.data = &ctx;

    memset(req, 0, sizeof(HttpRequest));  // zero fields

    /* execute the parser over the raw received buffer */
    llhttp_errno_t err = llhttp_execute(&parser, buffer, buffer_len);

    /* Store last header if not already stored */
    if(ctx.current_field[0] && ctx.req->header_count < HTTP_MAX_HEADER_COUNT)
    {
        int i = ctx.req->header_count++;

        /* Copy header name */
        size_t name_len =
            strnlen(ctx.current_field, HTTP_MAX_HEADER_NAME_LEN - 1);
        memcpy(ctx.req->header_names[i], ctx.current_field, name_len);
        ctx.req->header_names[i][name_len] = '\0';

        /* Copy header value */
        size_t value_len =
            strnlen(ctx.current_value, HTTP_MAX_HEADER_VALUE_LEN - 1);
        memcpy(ctx.req->header_values[i], ctx.current_value, value_len);
        ctx.req->header_values[i][value_len] = '\0';
    }

    if(err != HPE_OK)
    {
        log_error("[http]: llhttp parse error", llhttp_errno_name(err));
    }

    else
    {
        /* Set return to success */
        res = STATUS_SUCCESS;

#ifdef DEBUG_MODE
        log_info("[http]: parse request: METHOD: %s, PATH: %s, HEADERS: %d",
                 http_method_to_string(req->method), req->path,
                 req->header_count);
        // log_info("[http]: Parsed %d headers:", req->header_count);
        // for(int i = 0; i < req->header_count; ++i)
        // {
        //     log_info("[http]: %s: %s\n", req->header_names[i], req->header_values[i]);
        // }
#endif /* DEBUG_MODE */
    }

    return res;
}

static int validate_http_path(const char* path)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(!path || path[0] == '\0')
    {
        log_error("[http]: Request path is empty", "");
    }

    /* Check for path traversal attempts (any ".." in the path) */
    else if(strstr(path, "..") || strstr(path, "./"))
    {
        log_error("[http]: Path traversal attempt detected", path);
    }

    /* Check if path starts with . or ./ */
    else if(path[0] == '.' && (path[1] == '\0' || path[1] == '/'))
    {
        log_error("[http]: Path starts with '.': %s", path);
    }

    /* Check for dangerous or suspicious characters */
    else
    {
        /* assume valid unless find an issue */
        res = STATUS_SUCCESS;

        for(size_t i = 0; i < strlen(path); ++i)
        {
            unsigned char c = (unsigned char)path[i];

            /* Check for control characters, null byte, or suspicious characters */
            if(c < 0x20 || c == 0x7F)
            {
                log_error("[http]: Path contains control or null character %s",
                          path);
                res = STATUS_FAILURE;
                break;
            }

            /* Allow alphanumeric, '/', '-', ... */
            else if(!(isalnum(c) || c == '/' || c == '-' || c == '_' ||
                      c == '.' || c == '~' || c == '?' || c == '=' ||
                      c == '&' || c == '+' || c == '%'))
            {
                log_error("[http]: Path contains suspicious character %s",
                          path);
                res = STATUS_FAILURE;
                break;
            }

            /* If here - the character is valid */
        }
    }
    return res;
}

static int validate_http_header_name(const char* hname)
{
    if(!hname)
    {
        log_error("[http]: Null header name", "");
        return STATUS_FAILURE;
    }
    if(strlen(hname) >= HTTP_MAX_HEADER_NAME_LEN)
    {
        log_error("[http]: Header name too long", hname);
        return STATUS_FAILURE;
    }
    for(size_t j = 0; j < strlen(hname); ++j)
    {
        unsigned char c = (unsigned char)hname[j];
        if(c < 0x20 || c == 0x7F)
        {
            log_error("[http]: Header name contains control or null character",
                      hname);
            return STATUS_FAILURE;
        }
    }
    return STATUS_SUCCESS;
}

static int validate_http_header_value(const char* hval, const char* hname)
{
    if(!hval)
    {
        log_error("[http]: Null header value", hname ? hname : "");
        return STATUS_FAILURE;
    }
    if(strlen(hval) >= HTTP_MAX_HEADER_VALUE_LEN)
    {
        log_error("[http]: Header value too long", hname ? hname : "");
        return STATUS_FAILURE;
    }
    for(size_t j = 0; j < strlen(hval); ++j)
    {
        unsigned char c = (unsigned char)hval[j];
        if(c < 0x20 || c == 0x7F)
        {
            log_error("[http]: Header value contains control or null character",
                      hname ? hname : "");
            return STATUS_FAILURE;
        }
    }
    return STATUS_SUCCESS;
}

static int sanitize_http_request(HttpRequest* req)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Check for null pointers */
    if(!req)
    {
        log_error("[http]: Null pointer argument to sanitize_http_request", "");
    }

    /* Validate the request path */
    else if(validate_http_path(req->path) != STATUS_SUCCESS)
    {
        log_error("[http]: Validate path failed", req->path);
    }

    /* Ensure the method is valid */
    else if(req->method == HTTP_METHOD_UNKNOWN)
    {
        log_error("[http]: Invalid HTTP method HTTP_METHOD_UNKNOWN", "");
    }

    /* Ensure headers are within limits */
    else if(req->header_count > HTTP_MAX_HEADER_COUNT)
    {
        log_error("[http]: Too many headers", "");
        return STATUS_FAILURE;
    }

    else
    {
        res = STATUS_SUCCESS;

        /* Ensure header names and values are within limits and safe */
        for(int i = 0; i < req->header_count; ++i)
        {
            const char* hname = req->header_names[i];
            const char* hval  = req->header_values[i];
            if(validate_http_header_name(hname) != STATUS_SUCCESS)
            {
                log_error("[http]: Invalid header name", hname);
                res = STATUS_FAILURE;
                break;
            };
            if(validate_http_header_value(hval, hname) != STATUS_SUCCESS)
            {
                log_error("[http]: Invalid header value", hval);
                res = STATUS_FAILURE;
                break;
            }
        }

#ifdef DEBUG_MODE
        log_info("[http]: sanitizeD request: METHOD: %s, PATH: %s",
                 http_method_to_string(req->method), req->path);
#endif /* DEBUG_MODE */
    }

    return res;
}

static int on_url(llhttp_t* parser, const char* at, size_t length)
{
    LlhttpParserContext* ctx = (LlhttpParserContext*)parser->data;
    snprintf(ctx->req->path, HTTP_MAX_PATH_LEN, "%.*s", (int)length, at);
    return 0;
}

static int on_header_field(llhttp_t* parser, const char* at, size_t length)
{
    LlhttpParserContext* ctx = (LlhttpParserContext*)parser->data;

    // If were finishing a previous header, store it
    if(!ctx->in_header_field && ctx->req->header_count < HTTP_MAX_HEADER_COUNT)
    {
        int i = ctx->req->header_count++;
        strncpy(ctx->req->header_names[i], ctx->current_field,
                HTTP_MAX_HEADER_NAME_LEN);
        strncpy(ctx->req->header_values[i], ctx->current_value,
                HTTP_MAX_HEADER_VALUE_LEN);
        ctx->current_field[0] = 0;
        ctx->current_value[0] = 0;
    }

    snprintf(ctx->current_field, HTTP_MAX_HEADER_NAME_LEN, "%.*s", (int)length,
             at);
    ctx->in_header_field = 1;
    return 0;
}

static int on_header_value(llhttp_t* parser, const char* at, size_t length)
{
    LlhttpParserContext* ctx = (LlhttpParserContext*)parser->data;
    snprintf(ctx->current_value, HTTP_MAX_HEADER_VALUE_LEN, "%.*s", (int)length,
             at);
    ctx->in_header_field = 0;
    return 0;
}

static int on_body(llhttp_t* parser, const char* at, size_t length)
{
    LlhttpParserContext* ctx = (LlhttpParserContext*)parser->data;
    HttpRequest*         req = ctx->req;

    /* If this chunk would push us over the RAM cap, abort */
    if(req->body_len + length > HTTP_MAX_BODY_RAM_CAPACITY)
    {
        log_error(
            "[http]: Request body exceeds HTTP_MAX_BODY_RAM_CAPACITY (%d "
            "bytes)",
            HTTP_MAX_BODY_RAM_CAPACITY);

        /* drop any data we’d buffered so far */
        free(req->body);
        req->body     = NULL;
        req->body_len = 0;

        /* return non-zero to tell llhttp to stop parsing with error */
        return 1;
    }

    /* Otherwise append into RAM buffer */

    /* allocate or grow to exactly fit new size + null terminator */
    char* new_buf = realloc(req->body, req->body_len + length + 1);
    if(!new_buf)
    {
        log_error("[http]: Failed to reallocate memory for request body", "");

        /* Clean up on OOM */
        free(req->body);
        req->body     = NULL;
        req->body_len = 0;
        return 1;
    }

    /* copy in the new chunk and update length */
    memcpy(new_buf + req->body_len, at, length);
    req->body                 = new_buf;
    req->body_len            += length;
    req->body[req->body_len]  = '\0';

    /* success! */
    return 0;
}

static int on_method(llhttp_t* parser, const char* at, size_t length)
{
    LlhttpParserContext* ctx = (LlhttpParserContext*)parser->data;
    ctx->req->method         = parse_http_method(at, length);
    return 0;
}

static http_method_t parse_http_method(const char* at, size_t length)
{
    /* Return variable */
    http_method_t method = HTTP_METHOD_UNKNOWN;

    if(length > HTTP_MAX_METHOD_LEN)
    {
        log_error("[http]: HTTP method too long", "");
    }
    else if(length == 3 && strncmp(at, "GET", 3) == 0)
        method = HTTP_METHOD_GET;
    else if(length == 4 && strncmp(at, "POST", 4) == 0)
        method = HTTP_METHOD_POST;
    else if(length == 3 && strncmp(at, "PUT", 3) == 0)
        method = HTTP_METHOD_PUT;
    else if(length == 6 && strncmp(at, "DELETE", 6) == 0)
        method = HTTP_METHOD_DELETE;

    return method;
}

static void determine_connection_policy(HttpRequest* req)
{
    for(int i = 0; i < req->header_count; ++i)
    {
        /* Check for the header Connection */
        if(strcasecmp(req->header_names[i], "Connection") == 0 &&
           strcasestr(req->header_values[i], "close"))
        {
            req->connection_policy = HTTP_CONNECTION_CLOSE;
            break;
        }
    }
}
