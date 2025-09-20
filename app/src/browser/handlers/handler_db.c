/**
 * @file handler_db.c
 * @brief
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑05‑11
 * (c) 2025
 */
#define _POSIX_C_SOURCE 200809L
#include "handlers_int.h"
#include "db_interface.h"

#include <string.h>

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

static void hex16(const uint8_t id[DB_ID_SIZE], char out[33]);

static inline const char* status_from_rc(int rc)
{
    if(rc == 0)
        return "inserted";
    if(rc == -EEXIST)
        return "exists";
    return "error";
}

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int handler_db_add_user(const HttpRequest* request, HttpResponse* response)
{
    if(!request || !response)
    {
        log_error("[handler_db] invalid input");
        return STATUS_FAILURE;
    }
    if(request->method != HTTP_METHOD_PUT)
    {
        send_405(response);
        return STATUS_FAILURE;
    }
    if(!request->body || request->body_len == 0)
    {
        send_400(response);
        return STATUS_FAILURE;
    }

    cJSON* root = cJSON_ParseWithLength(request->body, request->body_len);
    if(!root) { send_400(response); return STATUS_FAILURE; }

    cJSON* jemail = cJSON_GetObjectItemCaseSensitive(root, "email");
    if(!cJSON_IsString(jemail) || !jemail->valuestring || jemail->valuestring[0] == '\0')
    { cJSON_Delete(root); send_400(response); return STATUS_FAILURE; }

    // in handler_db_add_user(...)
    const char* email_in = jemail->valuestring;

    char email_buf[DB_EMAIL_MAX_LEN];
    size_t n = strnlen(email_in, DB_EMAIL_MAX_LEN);
    if (n == 0 || n >= DB_EMAIL_MAX_LEN) {
        cJSON_Delete(root);
        send_400(response);   // too long / invalid
        return STATUS_FAILURE;
    }
    memcpy(email_buf, email_in, n + 1);  // include NUL

    uint8_t id[DB_ID_SIZE] = {0};
    int rc = db_add_user(email_buf, id);  // pass fixed-size buffer

    if(rc != 0 && rc != -EEXIST)
    { cJSON_Delete(root); send_500(response); return STATUS_FAILURE; }

    char idhex[33];
    hex16(id, idhex);

    cJSON* answ = cJSON_CreateObject();
    if(!answ) { cJSON_Delete(root); send_500(response); return STATUS_FAILURE; }

    cJSON* user = cJSON_CreateObject();
    if(!user) { cJSON_Delete(answ); cJSON_Delete(root); send_500(response); return STATUS_FAILURE; }

    cJSON_AddItemToObject(answ, "user", user); // ownership transferred

    // ---- split the adds for clear diagnostics ----
    if(!cJSON_AddStringToObject(user, "id", idhex))
    { log_error("[handler_db] add id failed"); goto fail; }

    if(!cJSON_AddStringToObject(user, "email", email_buf))
    { log_error("[handler_db] add email failed"); goto fail; }

    const char* status = status_from_rc(rc);
    if(!cJSON_AddStringToObject(answ, "status", status))
    { log_error("[handler_db] add status failed"); goto fail; }
    // ---------------------------------------------

    char* body = cJSON_PrintUnformatted(answ);
    if(!body) { goto fail; }

    response->status_code  = (rc == 0) ? 201 : 200;
    response->status_text  = (rc == 0) ? "Created" : "OK";
    response->content_type = "application/json";
    response->body         = body;
    response->body_length  = strlen(body);

    cJSON_Delete(answ);
    cJSON_Delete(root);
    return STATUS_SUCCESS;

fail:
    cJSON_Delete(answ);
    cJSON_Delete(root);
    send_500(response);
    return STATUS_FAILURE;
}

// int handler_db_list_users();

// int handler_db(const HttpRequest* request, HttpResponse* response)
// {
//     /* return value */
//     int res = STATUS_FAILURE;

//     return res;
// }

/****************************************************************************
 * PRIVATE FUNCTION DEFINITIONS
 ***************************************************************************
 */
/* None */

static void hex16(const uint8_t id[DB_ID_SIZE], char out[33])
{
    static const char* h = "0123456789abcdef";
    for(int i = 0; i < 16; i++)
    {
        out[i * 2]     = h[id[i] >> 4];
        out[i * 2 + 1] = h[id[i] & 0xF];
    }
    out[32] = '\0';
}

REGISTER_ROUTE("/api/db_add_user", handler_db_add_user)
