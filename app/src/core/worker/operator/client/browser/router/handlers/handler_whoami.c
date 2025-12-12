/**
 * handler_whoami.c
 * ----------------
 * Implements the /api/whoami endpoint handler for the web server.
 *
 * Responds with a JSON object containing:
 *   - The current server time (ISO-8601 with milliseconds)
 *   - The HTTP method and path
 *   - All request headers echoed back
 *
 * This handler receives a parsed Http_request_t and fills a fresh HttpResponse.
 * No socket I/O is performed here; the caller is responsible for network
 * transmission and memory cleanup.
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑05‑11
 * (c) 2025
 */
#define _GNU_SOURCE

#include <sys/time.h> /* gettimeofday() */
#include <time.h>     /* gmtime_r(), strftime() */

#include "handlers_int.h"
#include "yyjson.h"
#include "emlog.h"

#define LOG_TAG "handler_whoami"

static void _sv_to_cstr(char *dst, size_t cap, const sv_t *sv)
{
    if(!dst || cap == 0) return;
    size_t len = 0;
    if(sv && sv->p && sv->n > 0)
    {
        len = sv->n < (cap - 1) ? sv->n : (cap - 1);
        memcpy(dst, sv->p, len);
    }
    dst[len] = '\0';
}

/****************************************************************************
 * PRIVATE DEFINES
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
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int handler_whoami(const Http_request_t *req, HttpResponse *res)
{
    /*-----------------------------------------------------------------------
     * (1)  Compute an ISO‑8601 timestamp with milliseconds.                 *
     *---------------------------------------------------------------------*/
    struct timeval tv;
    struct tm      tm;
    char           timestr[32];

    gettimeofday(&tv, NULL);
    gmtime_r(&tv.tv_sec, &tm);
    strftime(timestr, sizeof timestr, "%Y-%m-%dT%H:%M:%S", &tm);

    char iso_time[32];
    int  len = snprintf(iso_time,
                        sizeof iso_time,
                        "%s.%03ldZ",
                        timestr,
                        tv.tv_usec / 1000L); /* µs → ms */

    if(len < 0 || len >= (int)sizeof iso_time)
    {
        strncpy(iso_time, timestr, sizeof iso_time - 1);
        iso_time[sizeof iso_time - 1] = '\0';
    }

    /*-----------------------------------------------------------------------
     * (2)  Build the JSON structure using yyjson.                           *
     *---------------------------------------------------------------------*/
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if(!doc)
    {
        send_500(res);
        return -1;
    }

    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    char path_buf[HTTP_MAX_PATH_LEN];
    _sv_to_cstr(path_buf, sizeof path_buf, &req->path);
    yyjson_mut_obj_add_str(doc, root, "path", path_buf);
    yyjson_mut_obj_add_str(doc, root, "contract_version", CONTRACT_VERSION);
    yyjson_mut_obj_add_str(doc, root, "server_time", iso_time);
    yyjson_mut_obj_add_str(doc, root, "method", http_method_to_string(req->method));

    yyjson_mut_val *hdrs = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "headers", hdrs);
    char header_name_buf[HTTP_MAX_HEADER_NAME_LEN];
    char header_value_buf[HTTP_MAX_HEADER_VALUE_LEN];
    for(int i = 0; i < req->header_count; ++i)
    {
        _sv_to_cstr(header_name_buf, sizeof header_name_buf, &req->header_names[i]);
        _sv_to_cstr(header_value_buf, sizeof header_value_buf, &req->header_values[i]);
        yyjson_mut_obj_add_str(doc, hdrs, header_name_buf, header_value_buf);
    }

    size_t body_len = 0;
    char  *body = yyjson_mut_write(doc, 0, &body_len);
    yyjson_mut_doc_free(doc);
    if(!body)
    {
        send_500(res);
        return -1;
    }

    /*-----------------------------------------------------------------------
     * (3)  Populate the HttpResponse structure for the caller.             *
     *---------------------------------------------------------------------*/
    res->status_code = 200;
    res->status_text = "OK";
    res->content_type = "application/json";
    res->body = body;
    res->body_length = body_len;
    res->body_owned = 1;

    return 0;
}

REGISTER_ROUTE("/api/whoami", handler_whoami)
