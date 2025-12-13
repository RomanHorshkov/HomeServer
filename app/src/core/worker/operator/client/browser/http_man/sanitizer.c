/**
 * @file sanitizer.c
 * @brief HTTP request sanitizer implementation.
 *
 * Called once per fully-parsed request (e.g. from llhttp on_message_complete).
 *
 * Checks:
 *   - method is one of our supported enums
 *   - path chars + length + traversal attempts
 *   - header count within limit
 *   - header names/values match their respective policy bitmaps
 *   - (optional) body size limit
 *
 * No allocations, all checks are simple loops + bitmap lookups.
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    dec 2025
 * 
 * (c) 2025
 */

 
#include "sanitizer.h"
#include "http_manager.h"

#include "sanitizer_policy.h"


/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
#define LOG_TAG "srv_http_sanitizer"

/****************************************************************************
 * PRIVATE ENUMERATED TYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE ENUMERATED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STRUCTURED TYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Check that all characters in the string view are allowed.
 * @return STATUS_SUCCESS if all characters are allowed, STATUS_FAILURE otherwise.
 */
static int _validate_http_path(const sv_t *path);

/**
 * @brief Check that all headers are valid.
 * @return STATUS_SUCCESS if all headers are valid, STATUS_FAILURE otherwise.
 */
static int _validate_http_headers(const http_request_t *req);

/**
 * @brief Check that header name and value contain only allowed characters.
 * @return STATUS_SUCCESS if valid, STATUS_FAILURE otherwise.
 */
static int _validate_http_header(const sv_t *name, const sv_t *value);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int sanitize_http_request(http_request_t *req)
{
    /* Check input */
    if(!req)
    {
        EML_ERROR(LOG_TAG, "sanitize_http_request: null req");
        return STATUS_FAILURE;
    }

    /* Check method */
    if(req->method >= HTTP_METHOD_UNKNOWN)
    {
        EML_ERROR(LOG_TAG, "Invalid/unsupported HTTP method");
        return STATUS_FAILURE;
    }

    /* Check header count */
    if(req->header_count > HTTP_MAX_HEADERS_IN)
    {
        EML_ERROR(LOG_TAG, "Too many headers");
        return STATUS_FAILURE;
    }

    /* Validate path */
    if(_validate_http_path(&req->path) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "Invalid HTTP path");
        return STATUS_FAILURE;
    }

    /* Validate each header */
    if(_validate_http_headers(req) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "Invalid HTTP headers");
        return STATUS_FAILURE;
    }


#ifdef MODE_DEBUG
    EML_INFO(LOG_TAG,
             "sanitized request: METHOD=%s PATH=%.*s headers=%u",
             http_method_to_string(req->method),
             (int)req->path.n, req->path.p ? req->path.p : "",
             req->header_count);
#endif

    return STATUS_SUCCESS;
}
/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
/* None */

static int _validate_http_path(const sv_t *path)
{
    if(!path || !path->p || path->n == 0)
    {
        EML_ERROR(LOG_TAG, "Request path is empty");
        return STATUS_FAILURE;
    }

    if(path->n >= HTTP_MAX_PATH_LEN)
    {
        EML_ERROR(LOG_TAG, "Path too long: %zu", path->n);
        return STATUS_FAILURE;
    }

    const char *p = path->p;
    size_t      n = path->n;

    /* Check for control characters or suspicious characters */
    if(!SANITIZER_POLICY_PATH_VALID(p, n))
    {
        EML_ERROR(LOG_TAG, "Invalid chars in HTTP path (policy)");
        return STATUS_FAILURE;
    }

    /* Simple traversal checks: ".." or "./" or starting with '.' */
    if(n >= 2 && p[0] == '.' && (p[1] == '/' || p[1] == '\0'))
    {
        EML_ERROR(LOG_TAG, "Path starts with '.'");
        return STATUS_FAILURE;
    }

    /* Check for ".." or "./" anywhere in the path */
    for(size_t i = 0; i + 1 < n; ++i)
    {
        if (p[i] == '.' && (p[i + 1] == '.' || p[i + 1] == '/'))
        {
            EML_ERROR(LOG_TAG, "Potential path traversal: %.*s",
                      (int)n, p);
            return STATUS_FAILURE;
        }
    }

    return STATUS_SUCCESS;
}

static int _validate_http_headers(const http_request_t *req)
{
    /* Validate each header */
    for(int i = 0; i < req->header_count; ++i)
    {
        const sv_t *name  = &req->header_names[i];
        const sv_t *value = &req->header_values[i];
        if(_validate_http_header(name, value) != STATUS_SUCCESS)
        {
            EML_ERROR(LOG_TAG, "Invalid HTTP header: %.*s",
                      (int)name->n,
                      name->p ? name->p : "<unknown>");
            return STATUS_FAILURE;
        }
    }

    return STATUS_SUCCESS;
}

static int _validate_http_header(const sv_t *name, const sv_t *value)
{
    /* Check input correctness */
    if(!name || !name->p || name->n == 0 || name->n >= HTTP_MAX_HEADER_NAME_LEN)
    {
        EML_ERROR(LOG_TAG, "http invalid header name");
        return STATUS_FAILURE;
    }

    if(!value || !value->p || value->n == 0)
    {
        EML_ERROR(LOG_TAG, "http invalid header value");
        return STATUS_FAILURE;
    }

    /* Check name len */
    if (name->n >= HTTP_MAX_HEADER_NAME_LEN)
    {
        EML_ERROR(LOG_TAG, "Header name too long: %zu", name->n);
        return STATUS_FAILURE;
    }
    
    /* Check for control characters or suspicious characters */
    /* Check name chars */
    if(!SANITIZER_POLICY_HDR_NAME_VALID(name->p, name->n))
    {
        EML_ERROR(LOG_TAG, "Invalid chars in header name: %.*s", (int)name->n, name->p);
        return STATUS_FAILURE;
    }

    /* Check value chars */
    if(!SANITIZER_POLICY_HDR_VALUE_VALID(value->p, value->n))
    {
        EML_ERROR(LOG_TAG, "Invalid chars in header value: %.*s", (int)value->n, value->p);
        return STATUS_FAILURE;
    }

    return STATUS_SUCCESS;
}
