/**
 * @file handler_db.c
 * @brief Bridge between the HTTP layer and the external DB application.
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "app_interface.h"
#include "handlers_int.h"
#include "http_manager.h"
#include "utils_interface.h"
#include "emlog.h"

#define LOG_TAG "handler_db"

static inline int _sv_starts_with_str(const sv_t *sv, const char *prefix)
{
    if(!sv || !sv->p || !prefix) return 0;
    size_t plen = strlen(prefix);
    if(sv->n < plen) return 0;
    return memcmp(sv->p, prefix, plen) == 0;
}

/****************************************************************************
 * PRIVATE HELPERS
 ****************************************************************************/

static inline int db_request_init_from_http(const Http_request_t* in, uint64_t now_epoch,
                                            db_hdr_kv_t* headers_out, DB_request_t* out);
static void http_response_from_db(HttpResponse* http_res, DB_response_t* db_res);
static void cleanup_db_response(DB_response_t* db_res);
static void copy_sv_to_buf(const sv_t* sv, char* dst, size_t cap);
static const char* http_reason_phrase(int status);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************/

int handler_database(const Http_request_t* req, HttpResponse* res)
{
    if(!req || !res)
    {
        EML_ERROR(LOG_TAG, "Invalid input to handler_database");
        send_400(res);
        return -1;
    }

    db_hdr_kv_t hdr_views[HTTP_MAX_HEADERS_IN];
    DB_request_t db_req;

    if(db_request_init_from_http(req, time(NULL), hdr_views, &db_req) != 0)
    {
        EML_ERROR(LOG_TAG, "Failed to initialize DB_request_t from Http_request_t");
        send_400(res);
        return -1;
    }

    DB_response_t db_res;
    memset(&db_res, 0, sizeof db_res);

    db_app_status_t status = db_app_run(&db_req, &db_res);
    http_response_from_db(res, &db_res);
    res->body_owned = 1;

    return (status == DB_APP_OK) ? 0 : -1;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************/

static inline int db_request_init_from_http(const Http_request_t* in, uint64_t now_epoch,
                                            db_hdr_kv_t* headers_out, DB_request_t* out)
{
    if(!in || !headers_out || !out) return -1;

    memset(out, 0, sizeof(*out));
    out->now_epoch = now_epoch;
    out->remote_ip_be = in->remote_ip_be;
    out->remote_port_be = in->remote_port_be;
    out->thread_id = in->thread_id;
    out->method = in->method;
    sv_t path = in->path;
    static const char prefix[] = "/api/app/";
    if(_sv_starts_with_str(&path, prefix))
    {
        path.p += sizeof(prefix) - 1;
        path.n -= (sizeof(prefix) - 1);
    }
    else if(_sv_starts_with_str(&path, "/api/app"))
    {
        const size_t skip = strlen("/api/app");
        path.p += skip;
        path.n -= skip;
    }
    if(path.p && path.n > 0 && path.p[0] == '/')
    {
        path.p++;
        path.n--;
    }
    out->path = path;

    int hc = (in->header_count < HTTP_MAX_HEADERS_IN) ? in->header_count : HTTP_MAX_HEADERS_IN;
    for(int i = 0; i < hc; ++i)
    {
        headers_out[i].key = in->header_names[i];
        headers_out[i].value = in->header_values[i];
    }
    out->headers = headers_out;
    out->header_count = hc;

    out->body = in->body.p;
    out->body_len = in->body.n;

    return 0;
}

static void http_response_from_db(HttpResponse* http_res, DB_response_t* db_res)
{
    if(!http_res || !db_res)
    {
        return;
    }

    http_res->status_code = db_res->status ? db_res->status : 500;
    http_res->status_text = http_reason_phrase(http_res->status_code);

    if(db_res->content_type.p && db_res->content_type.n > 0)
    {
        http_res->content_type = db_res->content_type.p;
    }
    else
    {
        http_res->content_type = "application/octet-stream";
    }

    if(db_res->body && db_res->body_len > 0)
    {
        http_res->body = db_res->body;
        http_res->body_length = db_res->body_len;
        http_res->body_owned = 1;
        db_res->body = NULL;
        db_res->body_len = 0;
    }
    else
    {
        http_res->body = NULL;
        http_res->body_length = 0;
        http_res->body_owned = 0;
    }

    http_res->header_count = 0;
    if(db_res->headers && db_res->header_count > 0)
    {
        int limit = db_res->header_count < HTTP_MAX_HEADERS_OUT ? db_res->header_count
                                                                : HTTP_MAX_HEADERS_OUT;
        for(int i = 0; i < limit; ++i)
        {
            copy_sv_to_buf(&db_res->headers[i].key, http_res->header_names[i],
                           HTTP_MAX_HEADER_NAME_LEN);
            copy_sv_to_buf(&db_res->headers[i].value, http_res->header_values[i],
                           HTTP_MAX_HEADER_VALUE_LEN);
            http_res->header_count++;
        }

        if(db_res->header_count > HTTP_MAX_HEADERS_OUT)
        {
            EML_ERROR(LOG_TAG, "Truncated headers %d→%d", (int)db_res->header_count,
                      HTTP_MAX_HEADERS_OUT);
        }
    }

    cleanup_db_response(db_res);
}

static void cleanup_db_response(DB_response_t* db_res)
{
    if(!db_res)
    {
        return;
    }

    if(db_res->headers)
    {
        for(size_t i = 0; i < (size_t)db_res->header_count; ++i)
        {
            const db_hdr_kv_t* h = &db_res->headers[i];
            if(h->key.n == 10 && strncasecmp(h->key.p, "Set-Cookie", 10) == 0)
            {
                free_cstr_p(h->value.p);
            }
        }
        free(db_res->headers);
        db_res->headers = NULL;
    }

    if(db_res->body)
    {
        free(db_res->body);
        db_res->body = NULL;
    }

    db_res->header_count = 0;
    db_res->content_type = (sv_t){0, 0};
}

static void copy_sv_to_buf(const sv_t* sv, char* dst, size_t cap)
{
    if(!dst || cap == 0)
    {
        return;
    }

    size_t len = 0;
    if(sv && sv->p && sv->n > 0)
    {
        len = sv->n < (cap - 1) ? sv->n : (cap - 1);
        memcpy(dst, sv->p, len);
    }
    dst[len] = '\0';
}

static const char* http_reason_phrase(int status)
{
    switch(status)
    {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 202:
            return "Accepted";
        case 204:
            return "No Content";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 409:
            return "Conflict";
        case 422:
            return "Unprocessable Entity";
        case 500:
            return "Internal Server Error";
        default:
            return "OK";
    }
}

/****************************************************************************
 * ROUTE REGISTRATION
 ****************************************************************************/
REGISTER_ROUTE("/api/app", handler_database)
