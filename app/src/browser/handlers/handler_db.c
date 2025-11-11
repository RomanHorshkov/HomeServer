/**
 * @file handler_db.c
 * @brief Bridge between the HTTP layer and the external DB application.
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "db_app/db_app.h"
#include "handlers_int.h"
#include "http_manager.h"
#include "utils_interface.h"

/****************************************************************************
 * PRIVATE HELPERS
 ****************************************************************************/

static inline int db_request_init_from_http(const HttpRequest* in, uint64_t now_epoch,
                                            uint32_t remote_ip_be, uint16_t remote_port_be,
                                            db_hdr_kv_t* headers_out, DB_request_t* out);
static void http_response_from_db(HttpResponse* http_res, DB_response_t* db_res);
static void cleanup_db_response(DB_response_t* db_res);
static void copy_sv_to_buf(const sv_t* sv, char* dst, size_t cap);
static const char* http_reason_phrase(int status);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************/

int handler_database(const HttpRequest* req, HttpResponse* res)
{
    if(!req || !res)
    {
        send_400(res);
        return -1;
    }

    db_hdr_kv_t hdr_views[HTTP_MAX_HEADERS_IN];
    DB_request_t db_req;

    if(db_request_init_from_http(req, time(NULL), 0, 0, hdr_views, &db_req) != 0)
    {
        send_400(res);
        return -1;
    }

    DB_response_t db_res;
    memset(&db_res, 0, sizeof db_res);

    db_app_status_t status = db_app_run(&db_req, &db_res);
    http_response_from_db(res, &db_res);

    return (status == DB_APP_OK) ? 0 : -1;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************/

static inline int db_request_init_from_http(const HttpRequest* in, uint64_t now_epoch,
                                            uint32_t remote_ip_be, uint16_t remote_port_be,
                                            db_hdr_kv_t* headers_out, DB_request_t* out)
{
    if(!in || !headers_out || !out) return -1;

    out->now_epoch = now_epoch;
    out->remote_ip_be = remote_ip_be;
    out->remote_port_be = remote_port_be;
    out->method = in->method;
    out->path = sv_c(in->path, HTTP_MAX_PATH_LEN);

    int hc = (in->header_count < HTTP_MAX_HEADERS_IN) ? in->header_count : HTTP_MAX_HEADERS_IN;
    for(int i = 0; i < hc; ++i)
    {
        headers_out[i].HDR_KEY = sv_c(in->header_names[i], HTTP_MAX_HEADER_NAME_LEN);
        headers_out[i].HDR_VAL = sv_c(in->header_values[i], HTTP_MAX_HEADER_VALUE_LEN);
    }
    out->headers = headers_out;
    out->header_count = hc;

    out->body = in->body;
    out->body_len = in->body_len;

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
        db_res->body = NULL;
        db_res->body_len = 0;
    }
    else
    {
        http_res->body = NULL;
        http_res->body_length = 0;
    }

    http_res->header_count = 0;
    if(db_res->headers && db_res->header_count > 0)
    {
        int limit = db_res->header_count < HTTP_MAX_HEADERS_OUT ? db_res->header_count
                                                                : HTTP_MAX_HEADERS_OUT;
        for(int i = 0; i < limit; ++i)
        {
            copy_sv_to_buf(&db_res->headers[i].HDR_KEY, http_res->header_names[i],
                           HTTP_MAX_HEADER_NAME_LEN);
            copy_sv_to_buf(&db_res->headers[i].HDR_VAL, http_res->header_values[i],
                           HTTP_MAX_HEADER_VALUE_LEN);
            http_res->header_count++;
        }

        if(db_res->header_count > HTTP_MAX_HEADERS_OUT)
        {
            log_error("[handler_db] truncated headers %d→%d", db_res->header_count,
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
            if(h->HDR_KEY.n == 10 && strncasecmp(h->HDR_KEY.p, "Set-Cookie", 10) == 0)
            {
                free_cstr_p(h->HDR_VAL.p);
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
REGISTER_ROUTE("/api/database", handler_database)
