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

#include "handlers_int.h"
#include "db_interface.h"
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

/****************************************************************************
 * PRIVATE FUNCTION PROTOTYPES
 ****************************************************************************/
static void hex16(const uint8_t id[DB_ID_SIZE], char out[33]);
static const char* status_from_rc(int rc);

/* endpoint handlers */
static int handler_login_user(const HttpRequest* req, HttpResponse* res);
static int handler_add_user(const HttpRequest* req, HttpResponse* res);

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
int handler_auth_db(const HttpRequest* req, HttpResponse* res)
{
    if(!req || !res)
        return STATUS_FAILURE;

    if(strncmp(req->path, "/api/db_add_user", 16) == 0 &&
       req->method == HTTP_METHOD_PUT)
        return handler_add_user(req, res);

    if(strncmp(req->path, "/api/login", 10) == 0 &&
       req->method == HTTP_METHOD_POST)
        return handler_login_user(req, res);

    send_404(res);
    return STATUS_FAILURE;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEINITIONS
 ****************************************************************************
 */

static int handler_add_user(const HttpRequest* req, HttpResponse* res)
{
    if(!req->body || req->body_len == 0)
    {
        send_400(res);
        return STATUS_FAILURE;
    }

    cJSON* root = cJSON_ParseWithLength(req->body, req->body_len);
    if(!root) { send_400(res); return STATUS_FAILURE; }

    cJSON* jemail = cJSON_GetObjectItemCaseSensitive(root, "email");
    if(!cJSON_IsString(jemail) || !jemail->valuestring || jemail->valuestring[0] == '\0')
    {
        cJSON_Delete(root);
        send_400(res);
        return STATUS_FAILURE;
    }

    char email_buf[DB_EMAIL_MAX_LEN];
    size_t n = strnlen(jemail->valuestring, DB_EMAIL_MAX_LEN);
    if(n == 0 || n >= DB_EMAIL_MAX_LEN)
    {
        cJSON_Delete(root);
        send_400(res);
        return STATUS_FAILURE;
    }
    memcpy(email_buf, jemail->valuestring, n + 1);

    uint8_t id[DB_ID_SIZE] = {0};
    int rc = db_add_user(email_buf, id);

    if(rc != 0 && rc != -EEXIST)
    {
        cJSON_Delete(root);
        send_500(res);
        return STATUS_FAILURE;
    }

    char idhex[33];
    hex16(id, idhex);

    cJSON* answ = cJSON_CreateObject();
    if(!answ) { cJSON_Delete(root); send_500(res); return STATUS_FAILURE; }

    cJSON* user = cJSON_CreateObject();
    if(!user) { cJSON_Delete(answ); cJSON_Delete(root); send_500(res); return STATUS_FAILURE; }

    cJSON_AddItemToObject(answ, "user", user);
    cJSON_AddStringToObject(user, "id", idhex);
    cJSON_AddStringToObject(user, "email", email_buf);
    cJSON_AddStringToObject(answ, "status", status_from_rc(rc));

    char* body = cJSON_PrintUnformatted(answ);
    if(!body) { cJSON_Delete(answ); cJSON_Delete(root); send_500(res); return STATUS_FAILURE; }

    res->status_code  = (rc == 0) ? 201 : 200;
    res->status_text  = (rc == 0) ? "Created" : "OK";
    res->content_type = "application/json";
    res->body         = body;
    res->body_length  = strlen(body);

    cJSON_Delete(answ);
    cJSON_Delete(root);
    return STATUS_SUCCESS;
}

static int handler_login_user(const HttpRequest* req, HttpResponse* res)
{
    // similar pattern as add_user
    send_501(res); // not implemented yet
    return STATUS_FAILURE;
}

/****************************************************************************
 * UTILITIES
 ****************************************************************************/
static void hex16(const uint8_t id[DB_ID_SIZE], char out[33])
{
    static const char* h = "0123456789abcdef";
    for(int i = 0; i < 16; i++)
    {
        out[i*2]     = h[id[i] >> 4];
        out[i*2 + 1] = h[id[i] & 0xF];
    }
    out[32] = '\0';
}

static const char* status_from_rc(int rc)
{
    if(rc == 0) return "inserted";
    if(rc == -EEXIST) return "exists";
    return "error";
}

/****************************************************************************
 * REGISTER ROUTE
 ****************************************************************************/
REGISTER_ROUTE("/api/auth_db", handler_auth_db)
