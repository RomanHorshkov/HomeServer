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


/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief llhttp callback: called when a new message begins.
 * 
 * Resets the parser context for a new request.
 * 
 * @param p Pointer to the llhttp parser.
 * @return 0 on success.
 */
static int on_message_begin(llhttp_t *p);

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

/**
 * @brief llhttp callback: called when headers are complete.
 *
 * Marks the end of header parsing.
 *
 * @param p Pointer to the llhttp parser.
 * @return 0 on success.
 */
static int on_headers_complete(llhttp_t *p);

/**
 * @brief llhttp callback: called when body data is parsed.
 *
 * Appends the parsed body data to the request's body buffer.
 *
 * @param parser   Pointer to the llhttp parser.
 * @param at       Pointer to the start of the body data.
 * @param length   Length of the body data.
 * @return         0 on success.
 */
static int on_body(llhttp_t* parser, const char* at, size_t length);

/**
 * @brief llhttp callback: called when a message completes.
 */
static int on_message_complete(llhttp_t* parser);

/**
 * @brief llhttp callback: called when the HTTP method is fully parsed.
 *
 * Sets the HTTP method in the request structure.
 *
 * @param parser   Pointer to the llhttp parser.
 * @return         0 on success.
 */
static int on_method_complete(llhttp_t* parser);

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

int http_manage_request(const char* recv_buf, const size_t buffer_len, http_request_t* request)
{
    /* result variable */
    int res = STATUS_FAILURE;

    /* Allocate memory and parse raw HTTP request into http_request_t struct */
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

int http_parser_init(llhttp_parser_t *pstate)
{
    /* Check input */
    if(!pstate)
    {
        EML_ERROR(LOG_TAG, "_init: invalid input");
        return STATUS_FAILURE;
    }

    llhttp_settings_t *parser_settings = &pstate->parser_settings;

    /* Initialize the settings object */
    llhttp_settings_init(parser_settings);

    /* Possible return values 0, -1, HPE_USER (llhttp_data_cb) */
    parser_settings->on_message_begin = on_message_begin;
    parser_settings->on_url = on_url;
    parser_settings->on_method_complete = on_method_complete;
    parser_settings->on_header_field = on_header_field;
    parser_settings->on_header_value = on_header_value;
    parser_settings->on_headers_complete = on_headers_complete;
    parser_settings->on_body = on_body;
    /* Possible return values 0, -1, `HPE_PAUSED` (llhttp_cb) */
    parser_settings->on_message_complete = on_message_complete;

    llhttp_init(&pstate->parser, HTTP_REQUEST, &pstate->parser_settings);

    /* Set parser context */
    pstate->parser.data = &pstate->parser_ctx;

    /* Initial reset */
    http_parser_reset(pstate);

    return STATUS_SUCCESS;
}

int http_parser_execute(llhttp_parser_t *pstate, const char *in_buf, size_t len)
{
    if(!pstate || !in_buf)
    {
        EML_ERROR(LOG_TAG, "_execute: invalid input");
        return STATUS_FAILURE;
    }

    llhttp_errno_t err = llhttp_execute(&pstate->parser, in_buf, len);
    if(err != HPE_OK)
    {
        EML_ERROR(LOG_TAG, "_execute error: %s for %s", llhttp_errno_name(err), llhttp_get_error_reason(&pstate->parser));
        return STATUS_FAILURE;
    }
    return STATUS_SUCCESS;
}

void http_parser_reset(llhttp_parser_t *pstate)
{
    if (!pstate)
    {
        EML_ERROR(LOG_TAG, "_reset: invalid input");
        return;
    }

    /* Clean whole context, not parser */
    memset(&pstate->parser_ctx, 0, sizeof(llhttp_parser_ctx_t));

    /* Reset an already initialized parser back to the start state, preserving the
     * existing parser type, callback settings, user data */
    llhttp_reset(&pstate->parser);
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEINITIONS
 ****************************************************************************
 */

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

static int sanitize_http_request(http_request_t* req)
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

static int on_message_begin(llhttp_t *p)
{
    llhttp_parser_ctx_t *ctx = (llhttp_parser_ctx_t *)p->data;
    if(!ctx) return HPE_OK;

    /* Reset an already initialized parser back to the start state, preserving the
     * existing parser type, callback settings, user data */
    llhttp_reset(p);

    return HPE_OK;
}

static int on_url(llhttp_t* parser, const char* at, size_t length)
{
    /* llhttp gives URL / request-target piece by piece via on_url */
    /* Check input length */
    if (length >= HTTP_MAX_PATH_LEN)
    {
        EML_ERROR(LOG_TAG, "URL length exceeds maximum allowed length");
        return HPE_INVALID_URL;
    }
    
    /* Get parser context */
    llhttp_parser_ctx_t* ctx = (llhttp_parser_ctx_t*)parser->data;
    if (!ctx) return HPE_OK;
    
    /* Store the URL in the request structure */
    sv_set(&ctx->req.path, at, length);
    
    return 0;
}

static int on_header_field(llhttp_t* parser, const char* at, size_t length)
{
    llhttp_parser_ctx_t *ctx = (llhttp_parser_ctx_t *)p->data;
    if (!ctx) return HPE_OK;

    /* Starting a new field after having a complete field+value?
       Commit the previous header first. */
    if (!ctx->in_header_field &&
        ctx->current_field.p && ctx->current_field.n &&
        ctx->current_value.p && ctx->current_value.n)
    {
        http_request_t *req = &ctx->req;
        if (req->header_count < HTTP_MAX_HEADERS_IN)
        {
            int idx = req->header_count++;
            req->header_names[idx]  = ctx->current_field;
            req->header_values[idx] = ctx->current_value;
        }
        sv_reset(&ctx->current_field);
        sv_reset(&ctx->current_value);
    }

    ctx->in_header_field = 1;
    sv_set(&ctx->current_field, at, length);
    return HPE_OK;
}

static int on_header_value(llhttp_t* parser, const char* at, size_t length)
{
    llhttp_parser_ctx_t *ctx = (llhttp_parser_ctx_t *)p->data;
    if (!ctx) return HPE_OK;

    ctx->in_header_field = 0;
    sv_append_or_reset(&ctx->current_value, at, length);
    return HPE_OK;
}

static int on_headers_complete(llhttp_t *p)
{
    llhttp_parser_ctx_t *ctx = (llhttp_parser_ctx_t *)p->data;
    if (!ctx) return HPE_OK;

    /*
    Note: storing method as a view into llhttp’s static string, not
    into the client buffer. That’s fine and IMO nicer.
    */
    http_request_t *req = &ctx->req;

    /* commit last header if we have both field+value */
    if (ctx->current_field.p && ctx->current_field.n &&
        ctx->current_value.p && ctx->current_value.n &&
        req->header_count < HTTP_MAX_HEADERS_IN)
    {
        int idx = req->header_count++;
        req->header_names[idx]  = ctx->current_field;
        req->header_values[idx] = ctx->current_value;
    }

    /* copy URL view into request path */
    req->path = ctx->current_url;

    /* method: use llhttp's method name (static strings) */
    const char *mname = llhttp_method_name(p->method); /* from llhttp.h */
    req->method.p = mname;
    req->method.n = strlen(mname);

    /* connection policy: either use headers or llhttp_keep_alive */
    if (llhttp_should_keep_alive(p))
        req->connection_policy = HTTP_CONNECTION_KEEP_ALIVE;
    else
        req->connection_policy = HTTP_CONNECTION_CLOSE;

    return HPE_OK;
}

static int on_body(llhttp_t *p, const char *at, size_t length)
{
    llhttp_parser_ctx_t *ctx = (llhttp_parser_ctx_t *)p->data;
    if (!ctx) return HPE_OK;

    sv_append(&ctx->current_body, at, length);
    return HPE_OK;
}


static int on_message_complete(llhttp_t* parser)
{

    // /* Sanitize parsed HTTP request */
    // if(sanitize_http_request(request) != STATUS_SUCCESS)
    // {
    //     EML_PERR(LOG_TAG, "[browser] sanitize_http_request failed");
    //     goto fail;
    // }
    
    // determine_connection_policy(request);
}


static int on_method_complete(llhttp_t* parser)
{
    llhttp_parser_ctx_t* ctx = (llhttp_parser_ctx_t*)parser->data;
    ctx->req.method = llhttp_method_from_string(at, length);
    return 0;
}

static int on_message_complete_llhttp(llhttp_t *p)
{
    llhttp_parser_ctx_t *ctx = (llhttp_parser_ctx_t *)p->data;
    if (!ctx) return HPE_OK;

    http_request_t *req = &ctx->req;

    /* finalize body view */
    req->body = ctx->current_body;

    /* mark message complete */
    req->message_complete = 1u;

    return HPE_OK;
}
