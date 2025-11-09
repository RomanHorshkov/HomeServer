/**
 * @file handler_auth_db.c
 * @brief Unified Auth/DB handler for REST API.
 *
 * Handles user registration, login, and DB-related queries.
 *
 * @author Roman
 * @date 2025-10-04
 * (c) 2025
 */
#ifndef DB_REQUEST_H
#define DB_REQUEST_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "route_register.h"

/* -------- String view (no allocation, no copy) -------- */
typedef struct {
    const char* p;   /* may be NULL */
    size_t      n;   /* bytes (no trailing NUL required) */
} sv_t;

/* Safe helper to make a view from a C string (up to maxlen) */
static inline sv_t sv_c(const char* z, size_t maxlen) {
    if(!z) return (sv_t){0,0};
    size_t n = 0;
    while(n < maxlen && z[n] != '\0') n++;
    return (sv_t){ z, n };
}

/* Header pair: name and value, both as views */
typedef struct {
    sv_t name;
    sv_t value;
} db_hdr_kv_t;

typedef enum
{
    DB_APP_OK        = 0,
    DB_APP_BAD_REQ   = 1,
    DB_APP_UNAUTH    = 2,
    DB_APP_FORBIDDEN = 3,
    DB_APP_NOT_FOUND = 4,
    DB_APP_CONFLICT  = 5,
    DB_APP_INTERNAL  = 9
} db_app_status_t;

/* -------- Pass-through request for the DB layer -------- */
typedef struct {
    /* Transport/meta (optional, just forwarded as-is) */
    uint64_t now_epoch;     /* e.g., time(NULL) once per request */
    uint32_t remote_ip_be;  /* optional IPv4 (network byte order), else 0 */
    uint16_t remote_port_be;/* optional (network byte order), else 0 */

    /* Request line */
    int   method; /* your http_method_t enum value */
    sv_t  path;   /* clean URL only; no query kept here */

    /* Headers as-is (no normalization/indexing) */
    db_hdr_kv_t* headers;   /* pointer to an array of pairs (views only) */
    int          header_count;

    /* Body as-is (raw bytes) */
    const char* body;       /* may be NULL */
    size_t      body_len;   /* bytes */
} DB_request_t;

#endif /* DB_REQUEST_H */

// #include "db_app.h"
#include "http_manager.h"

#include <string.h>
#include <stdlib.h>

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
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

static inline int db_request_init_from_http(const HttpRequest* in,
                                            uint64_t now_epoch,
                                            uint32_t remote_ip_be,
                                            uint16_t remote_port_be,
                                            db_hdr_kv_t* headers_out,
                                            DB_request_t* out)
{
    if(!in || !headers_out || !out) return -1;

    out->now_epoch      = now_epoch;
    out->remote_ip_be   = remote_ip_be;
    out->remote_port_be = remote_port_be;

    out->method = in->method;

    /* path as a view into in->path (bounded by HTTP_MAX_PATH_LEN) */
    out->path = sv_c(in->path, HTTP_MAX_PATH_LEN);

    /* headers: turn each NUL-terminated string into views */
    int hc = (in->header_count < HTTP_MAX_HEADERS_IN) ? in->header_count
                                                      : HTTP_MAX_HEADERS_IN;
    for(int i = 0; i < hc; ++i) {
        headers_out[i].name  = sv_c(in->header_names[i],  HTTP_MAX_HEADER_NAME_LEN);
        headers_out[i].value = sv_c(in->header_values[i], HTTP_MAX_HEADER_VALUE_LEN);
    }
    out->headers      = headers_out;
    out->header_count = hc;

    /* body straight through */
    out->body     = in->body;
    out->body_len = in->body_len;

    return 0;
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
int handler_database(const HttpRequest* req, HttpResponse* res)
{
    if(!req || !res){ send_400(res); return -1; }

    /* Stack-only, no heap churn */
    db_hdr_kv_t hdr_views[HTTP_MAX_HEADERS_IN];
    DB_request_t db_req;

    if(db_request_init_from_http(req, time(NULL), 0, 0, hdr_views, &db_req) != 0)
    {
        send_400(res); return -1;
    }
    
    db_app_status_t st = db_app_call(&db_req, &res);
    
    return 0;
}
/****************************************************************************
 * PRIVATE FUNCTIONS DEINITIONS
 ****************************************************************************
 */

static const char* http_get_header(const HttpRequest* req, const char* name)
{
    for(int i=0;i<req->header_count;i++)
        if(strcasecmp(req->header_names[i], name)==0)
            return req->header_values[i];
    return NULL;
}


/****************************************************************************
 * REGISTER ROUTE
 ****************************************************************************/
REGISTER_ROUTE("/api/database", handler_database)

// handler_database.c
