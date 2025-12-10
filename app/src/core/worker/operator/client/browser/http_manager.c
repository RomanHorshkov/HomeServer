/**
 * @file http_manager.c
 * @brief Parses and manages raw HTTP requests.
 *
 * Uses llhttp to extract method, path, headers, and body from incoming data.
 * Enforces basic security checks (no path traversal, safe headers) and determines
 * connection policy (keep-alive or close) before returning a structured Http_request_t.
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE */

#include "http_manager.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>  // malloc(), free()

#include "llhttp.h"
#include "emlog.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "http_man"

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
    Http_request_t* req;
    char current_field[HTTP_MAX_HEADER_NAME_LEN];
    char current_value[HTTP_MAX_HEADER_VALUE_LEN];
    int in_header_field;

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
 * @brief llhttp callback: called when a message completes.
 */
static int on_message_complete(llhttp_t* parser);

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
 * @brief Parses a raw HTTP request buffer into an Http_request_t structure.
 *
 * This function processes the provided HTTP request buffer, extracting the HTTP method,
 * path, headers, and other relevant information, and populates the given Http_request_t struct.
 *
 * @param buffer      Pointer to the raw HTTP request buffer.
 * @param buffer_len  Length of the buffer in bytes.
 * @param req         Pointer to the Http_request_t structure to populate.
 * @return            0 on success, negative value on failure.
 */
static int http_parse_request(const char* buffer, const size_t buffer_len, Http_request_t* req);

/**
 * @brief Validates and sanitizes a parsed Http_request_t structure.
 *
 * This function checks the Http_request_t for security and protocol compliance, including:
 *   - Non-empty and safe path (no traversal, no control chars)
 *   - Valid HTTP method
 *   - Header count and length limits
 *   - Safe header names and values (no control chars, no nulls)
 *   - Logs any issues found
 *
 * @param req         Pointer to the parsed Http_request_t structure to validate.
 * @return            STATUS_SUCCESS if valid, STATUS_FAILURE otherwise.
 */
static int sanitize_http_request(Http_request_t* req);

/**
 * @brief Determines the connection policy (keep-alive or close) from headers.
 *
 * Scans the parsed headers for "Connection: close" and sets the policy in the request.
 *
 * @param req      Pointer to the Http_request_t structure to update.
 */
static void determine_connection_policy(Http_request_t* req);

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

int http_manage_request(const char* recv_buf, const size_t buffer_len, Http_request_t* request)
{
    /* result variable */
    int res = STATUS_FAILURE;

    /* Allocate memory and parse raw HTTP request into Http_request_t struct */
    if(http_parse_request(recv_buf, buffer_len, request) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "[browser] parse failed");
        goto fail;
    }

    /* Sanitize parsed HTTP reuest */
    if(sanitize_http_request(request) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "[browser] sanitize_http_request failed");
        goto fail;
    }

    /* Determine connection policy based on headers */
    determine_connection_policy(request);

    return STATUS_SUCCESS;

fail:
    return res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEINITIONS
 ****************************************************************************
 */

static int http_parse_request(const char* buffer, const size_t buffer_len, Http_request_t* req)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* llhttp parser declare */
    llhttp_t parser;
    llhttp_settings_t settings;

    /* llhttp parser initialize */
    llhttp_settings_init(&settings);

    settings.on_url = on_url;
    settings.on_method = on_method;
    settings.on_header_field = on_header_field;
    settings.on_header_value = on_header_value;
    settings.on_body = on_body;

    llhttp_init(&parser, HTTP_REQUEST, &settings);

    /* Create parsing context to keep track of parsing */
    LlhttpParserContext ctx = {.req = req, .in_header_field = 1};

    /* Assign the context to parser */
    parser.data = &ctx;

    memset(req, 0, sizeof(Http_request_t));  // zero fields

    /* execute the parser over the raw received buffer */
    llhttp_errno_t err = llhttp_execute(&parser, buffer, buffer_len);

    /* Store last header if not already stored */
    if(ctx.current_field[0] && ctx.req->header_count < HTTP_MAX_HEADERS_IN)
    {
        int i = ctx.req->header_count++;

        /* Copy header name */
        size_t name_len = strnlen(ctx.current_field, HTTP_MAX_HEADER_NAME_LEN - 1);
        memcpy(ctx.req->header_names[i], ctx.current_field, name_len);
        ctx.req->header_names[i][name_len] = '\0';

        /* Copy header value */
        size_t value_len = strnlen(ctx.current_value, HTTP_MAX_HEADER_VALUE_LEN - 1);
        memcpy(ctx.req->header_values[i], ctx.current_value, value_len);
        ctx.req->header_values[i][value_len] = '\0';
    }

    if(err != HPE_OK)
    {
        EML_ERROR(LOG_TAG, "llhttp parse error: %s", llhttp_errno_name(err));
    }

    else
    {
        /* Set return to success */
        res = STATUS_SUCCESS;

#ifdef DEBUG_MODE
        EML_INFO(LOG_TAG, "parse request: METHOD: %s, PATH: %s, HEADERS: %d",
                 http_method_to_string(req->method), req->path, req->header_count);
        // EML_INFO(LOG_TAG, "Parsed %d headers:", req->header_count);
        // for(int i = 0; i < req->header_count; ++i)
        // {
        //     EML_INFO(LOG_TAG, "%s: %s\n", req->header_names[i], req->header_values[i]);
        // }
#endif /* DEBUG_MODE */
    }

    return res;
}

int http_parser_init(http_parser_t *pstate)
{
    if(!pstate) return STATUS_FAILURE;
    memset(pstate, 0, sizeof(*pstate));

    llhttp_settings_init(&pstate->settings);
    pstate->settings.on_url = on_url;
    pstate->settings.on_method = on_method;
    pstate->settings.on_header_field = on_header_field;
    pstate->settings.on_header_value = on_header_value;
    pstate->settings.on_body = on_body;
    pstate->settings.on_message_complete = on_message_complete;

    llhttp_init(&pstate->parser, HTTP_REQUEST, &pstate->settings);

    /* TODO : CHECK IF NEEDS CALLOC HERE */
    /* attach context */
    LlhttpParserContext *ctx = calloc(1, sizeof(*ctx));
    if(!ctx) return STATUS_FAILURE;
    ctx->req = &pstate->req;
    pstate->parser.data = ctx;

    return STATUS_SUCCESS;
}

void http_parser_reset(http_parser_t *pstate)
{
    if(!pstate) return;
    LlhttpParserContext *ctx = (LlhttpParserContext *)pstate->parser.data;
    if(ctx)
    {
        memset(ctx->current_field, 0, sizeof(ctx->current_field));
        memset(ctx->current_value, 0, sizeof(ctx->current_value));
        ctx->in_header_field = 1;
    }
    memset(&pstate->req, 0, sizeof(pstate->req));
    pstate->req.body_len = 0;
    pstate->req.message_complete = 0;
    llhttp_reset(&pstate->parser);
}

int http_parser_execute(http_parser_t *pstate, const char *buf, size_t len)
{
    if(!pstate || !buf) return STATUS_FAILURE;
    llhttp_errno_t err = llhttp_execute(&pstate->parser, buf, len);
    if(err != HPE_OK)
    {
        EML_ERROR(LOG_TAG, "llhttp parse error: %s", llhttp_errno_name(err));
        return STATUS_FAILURE;
    }
    return STATUS_SUCCESS;
}

static int validate_http_path(const char* path)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(!path || path[0] == '\0')
    {
        EML_ERROR(LOG_TAG, "Request path is empty");
    }

    /* Check for path traversal attempts (any ".." in the path) */
    else if(strstr(path, "..") || strstr(path, "./"))
    {
        EML_ERROR(LOG_TAG, "Path traversal attempt detected: %s", path);
    }

    /* Check if path starts with . or ./ */
    else if(path[0] == '.' && (path[1] == '\0' || path[1] == '/'))
    {
        EML_ERROR(LOG_TAG, "Path starts with '.': %s", path);
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
                EML_ERROR(LOG_TAG, "Path contains control or null character %s", path);
                res = STATUS_FAILURE;
                break;
            }

            /* Allow alphanumeric, '/', '-', ... */
            else if(!(isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' ||
                      c == '?' || c == '=' || c == '&' || c == '+' || c == '%'))
            {
                EML_ERROR(LOG_TAG, "Path contains suspicious character %s", path);
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
        EML_ERROR(LOG_TAG, "Null header name");
        return STATUS_FAILURE;
    }
    if(strlen(hname) >= HTTP_MAX_HEADER_NAME_LEN)
    {
        EML_ERROR(LOG_TAG, "Header name too long: %s", hname);
        return STATUS_FAILURE;
    }
    for(size_t j = 0; j < strlen(hname); ++j)
    {
        unsigned char c = (unsigned char)hname[j];
        if(c < 0x20 || c == 0x7F)
        {
            EML_ERROR(LOG_TAG, "Header name contains control or null character: %s", hname);
            return STATUS_FAILURE;
        }
    }
    return STATUS_SUCCESS;
}

static int validate_http_header_value(const char* hval, const char* hname)
{
    if(!hval)
    {
        EML_ERROR(LOG_TAG, "Null header value (%s)", hname ? hname : "<unknown>");
        return STATUS_FAILURE;
    }
    if(strlen(hval) >= HTTP_MAX_HEADER_VALUE_LEN)
    {
        EML_ERROR(LOG_TAG, "Header value too long (%s)", hname ? hname : "<unknown>");
        return STATUS_FAILURE;
    }
    for(size_t j = 0; j < strlen(hval); ++j)
    {
        unsigned char c = (unsigned char)hval[j];
        if(c < 0x20 || c == 0x7F)
        {
            EML_ERROR(LOG_TAG, "Header value contains control or null character (%s)",
                      hname ? hname : "<unknown>");
            return STATUS_FAILURE;
        }
    }
    return STATUS_SUCCESS;
}

static int sanitize_http_request(Http_request_t* req)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Check for null pointers */
    if(!req)
    {
        EML_ERROR(LOG_TAG, "Null pointer argument to sanitize_http_request");
    }

    /* Validate the request path */
    else if(validate_http_path(req->path) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "Validate path failed: %s", req->path);
    }

    /* Ensure the method is valid */
    else if(req->method == HTTP_METHOD_UNKNOWN)
    {
        EML_ERROR(LOG_TAG, "Invalid HTTP method HTTP_METHOD_UNKNOWN");
    }

    /* Ensure headers are within limits */
    else if(req->header_count > HTTP_MAX_HEADERS_IN)
    {
        EML_ERROR(LOG_TAG, "Too many headers");
        return STATUS_FAILURE;
    }

    else
    {
        res = STATUS_SUCCESS;

        /* Ensure header names and values are within limits and safe */
        for(int i = 0; i < req->header_count; ++i)
        {
            const char* hname = req->header_names[i];
            const char* hval = req->header_values[i];
            if(validate_http_header_name(hname) != STATUS_SUCCESS)
            {
                EML_ERROR(LOG_TAG, "Invalid header name: %s", hname);
                res = STATUS_FAILURE;
                break;
            };
            if(validate_http_header_value(hval, hname) != STATUS_SUCCESS)
            {
                EML_ERROR(LOG_TAG, "Invalid header value: %s", hval);
                res = STATUS_FAILURE;
                break;
            }
        }

#ifdef DEBUG_MODE
        EML_INFO(LOG_TAG, "sanitizeD request: METHOD: %s, PATH: %s",
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
    if(!ctx->in_header_field && ctx->req->header_count < HTTP_MAX_HEADERS_IN)
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

static int on_header_value(llhttp_t* parser, const char* at, size_t length)
{
    LlhttpParserContext* ctx = (LlhttpParserContext*)parser->data;
    snprintf(ctx->current_value, HTTP_MAX_HEADER_VALUE_LEN, "%.*s", (int)length, at);
    ctx->in_header_field = 0;
    return 0;
}

static int on_body(llhttp_t* parser, const char* at, size_t length)
{
    LlhttpParserContext* ctx = (LlhttpParserContext*)parser->data;
    Http_request_t* req = ctx->req;

    /* If this chunk would push us over the RAM cap, abort */
    if(req->body_len + length > HTTP_MAX_BODY_RAM_CAPACITY)
    {
        EML_ERROR(LOG_TAG, 
            "Request body exceeds HTTP_MAX_BODY_RAM_CAPACITY (%d "
            "bytes)",
            (int)HTTP_MAX_BODY_RAM_CAPACITY);

        /* return non-zero to tell llhttp to stop parsing with error */
        return 1;
    }

    /* Otherwise append into preallocated buffer */
    memcpy(req->body + req->body_len, at, length);
    req->body_len += length;
    req->body[req->body_len] = '\0';

    /* success! */
    return 0;
}

static int on_method(llhttp_t* parser, const char* at, size_t length)
{
    LlhttpParserContext* ctx = (LlhttpParserContext*)parser->data;
    ctx->req->method = parse_http_method(at, length);
    return 0;
}

static int on_message_complete(llhttp_t* parser)
{
    LlhttpParserContext* ctx = (LlhttpParserContext*)parser->data;
    if(!ctx) return 0;
    ctx->req->message_complete = 1;
    return 0;
}

static http_method_t parse_http_method(const char* at, size_t length)
{
    /* Return variable */
    http_method_t method = HTTP_METHOD_UNKNOWN;

    if(length > HTTP_MAX_METHOD_LEN)
    {
        EML_ERROR(LOG_TAG, "HTTP method too long");
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

static void determine_connection_policy(Http_request_t* req)
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
