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

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************/

int handler_database(const Http_request_t* req, HttpResponse* res)
{
    if(!req || !res)
    {
        EML_ERROR(LOG_TAG, "Invalid input to handler_database");
        return -1;
    }

    db_hdr_kv_t hdr_views[HTTP_MAX_HEADERS_IN];
    DB_request_t db_req;

    if(db_request_init_from_http(req, time(NULL), hdr_views, &db_req) != 0)
    {
        EML_ERROR(LOG_TAG, "Failed to initialize DB_request_t from Http_request_t");
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

    EML_ERROR(LOG_TAG, "db_request_init_from_http: NOT IMPLEMENTED");
    return -1;
}
/****************************************************************************
 * ROUTE REGISTRATION
 ****************************************************************************/
REGISTER_ROUTE("/api/app", handler_database)
