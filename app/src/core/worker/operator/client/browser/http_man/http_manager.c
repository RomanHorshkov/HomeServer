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

#include <stdlib.h>  // malloc(), free()

#include "sanitizer.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "http_man"

#define PARSER_CTX(parser)  ((llhttp_parser_ctx_t *)((parser)->data))


/**
 * @brief Append to same sv if already present, i.e. input coming in chunks
 * otherwise set new.
 */
#define PARSER_SAVE(ctx_sv, at, length) \
    do { if(!(ctx_sv).p) sv_set(&(ctx_sv), at, length); \
    else sv_append(&(ctx_sv), at, length); } while(0)

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
 * @brief Commit the current header field and value to the request if both are ready.
 *
 * @param ctx Pointer to the llhttp parser context.
 */
static inline void _commit_header_if_ready(llhttp_parser_ctx_t *ctx)
{
    http_request_t *req = &ctx->req;

    /* Check if valid name, value and max headers constraint */
    if (ctx->current_field.p && ctx->current_field.n &&
        ctx->current_value.p && ctx->current_value.n &&
        req->header_count < HTTP_MAX_HEADERS_IN)
    {
        int idx = req->header_count++;
        /* Store the header field and value in the request structure */
        req->header_names[idx]  = ctx->current_field;
        req->header_values[idx] = ctx->current_value;
    }

    /* Reset current field and value for next header */
    sv_reset(&ctx->current_field);
    sv_reset(&ctx->current_value);
}

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int http_man_init(llhttp_parser_t *pstate)
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
    http_man_reset(pstate);

    return STATUS_SUCCESS;
}

int http_man_execute(llhttp_parser_t *pstate, const char *in_buf, size_t len)
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

void http_man_reset(llhttp_parser_t *pstate)
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

static int on_message_begin(llhttp_t *parser)
{
    /* Get parser context */
    llhttp_parser_ctx_t *ctx = PARSER_CTX(parser);
    if(!ctx)
    {
        EML_ERROR(LOG_TAG, "on_message_begin: null context");
        return HPE_USER;
    }

    memset(&ctx->req, 0, sizeof(http_request_t));
    sv_reset(&ctx->current_field);
    sv_reset(&ctx->current_value);
    sv_reset(&ctx->current_url);
    sv_reset(&ctx->current_body);
    ctx->in_header_field = 0;

    return HPE_OK;
}

static int on_url(llhttp_t* parser, const char* at, size_t length)
{
    /* llhttp gives URL / request-target piece by piece via on_url */
    /* Get parser context */
    llhttp_parser_ctx_t *ctx = PARSER_CTX(parser);
    if(!ctx)
    {
        EML_ERROR(LOG_TAG, "on_url: null context");
        return HPE_USER;
    }

    /* Append or set new */
    PARSER_SAVE(ctx->current_url, at, length);

    return HPE_OK;
}

static int on_header_field(llhttp_t* parser, const char* at, size_t length)
{
    /* Get parser context */
    llhttp_parser_ctx_t *ctx = PARSER_CTX(parser);
    if(!ctx)
    {
        EML_ERROR(LOG_TAG, "on_header_field: null context");
        return HPE_USER;
    }

    /* Starting a new field after having a complete field+value?
       Commit the previous header first. */
    if (!ctx->in_header_field &&
        ctx->current_field.p && ctx->current_field.n &&
        ctx->current_value.p && ctx->current_value.n)
    {
        _commit_header_if_ready(ctx);
    }

    /* Now we are in a header field */
    ctx->in_header_field = 1;

    /* Append or set new */
    PARSER_SAVE(ctx->current_field, at, length);

    return HPE_OK;
}

static int on_header_value(llhttp_t* parser, const char* at, size_t length)
{
    /* Get parser context */
    llhttp_parser_ctx_t *ctx = PARSER_CTX(parser);
    if(!ctx)
    {
        EML_ERROR(LOG_TAG, "on_header_value: null context");
        return HPE_USER;
    }

    /* Now we are in a header value */
    ctx->in_header_field = 0;

    /* Append or set new */
    PARSER_SAVE(ctx->current_value, at, length);

    return HPE_OK;
}

static int on_headers_complete(llhttp_t *parser)
{
    /* Get parser context */
    llhttp_parser_ctx_t *ctx = PARSER_CTX(parser);
    if(!ctx)
    {
        EML_ERROR(LOG_TAG, "on_headers_complete: null context");
        return HPE_USER;
    }

    /*
    Note: storing method as a view into llhttp’s static string, not
    into the client buffer. That’s fine and IMO nicer.
    */
    http_request_t *req = &ctx->req;

    /* Commit last header if pending */
    _commit_header_if_ready(ctx);

    /* set URL view into request path */
    req->path = ctx->current_url;
    
    /* Connection policy: let llhttp decide */
    if(llhttp_should_keep_alive(parser))
        req->connection_policy = HTTP_CONNECTION_KEEP_ALIVE;
    else
        req->connection_policy = HTTP_CONNECTION_CLOSE;

    /* SHOULD REJECT ANYTHING HERE? */

    return HPE_OK;
}

static int on_body(llhttp_t *parser, const char *at, size_t length)
{
    /* Get parser context */
    llhttp_parser_ctx_t *ctx = PARSER_CTX(parser);
    if(!ctx)
    {
        EML_ERROR(LOG_TAG, "on_body: null context");
        return HPE_USER;
    }

    /* Append or set new */
    PARSER_SAVE(ctx->current_body, at, length);

    return HPE_OK;
}

static int on_message_complete(llhttp_t* parser)
{
    /* Get parser context */
    llhttp_parser_ctx_t *ctx = PARSER_CTX(parser);
    if(!ctx)
    {
        EML_ERROR(LOG_TAG, "on_message_complete: null context");
        return HPE_USER;
    }

    /* Sanitize parsed HTTP request */
    if(sanitize_http_request(&ctx->req) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "sanitize_http_request failed");
        return HPE_INTERNAL;
    }

    /* Finalize body view */
    ctx->req.body = ctx->current_body;
    ctx->req.message_complete = 1u;

    return HPE_OK;
}


static int on_method_complete(llhttp_t* parser)
{
    /* Get parser context */
    llhttp_parser_ctx_t *ctx = PARSER_CTX(parser);
    if(!ctx)
    {
        EML_ERROR(LOG_TAG, "on_method_complete: null context");
        return HPE_USER;
    }

    /* Map llhttp method to the small enum */
    http_method_t lm = http_method_from_llhttp_method(llhttp_get_method(parser));

    if(lm == HTTP_METHOD_UNKNOWN)
    {
        EML_ERROR(LOG_TAG, "Unknown HTTP method: %d", lm);
        return HPE_INVALID_METHOD;
    }

    ctx->req.method = lm;
    return 0;
}
