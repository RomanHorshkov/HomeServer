/**
 * handler_drive.c
 * ---------------
 * Implements the /api/drive endpoint handler for the web server.
 *
 * Handles drive-related HTTP requests such as directory listing, file
 * upload, or file download. Utilizes OS filesystem primitives for
 * reading/writing.
 *
 * No direct network I/O is performed here; the caller is responsible for
 * sending responses.
 *
 *   @author  Roman Horshkov <roman.horshkov@gmail.com>
 *   @date    2025‑05‑11
 *   (c) 2025
 */

#ifndef SERVER_HANDLER_DRIVE_H
#define SERVER_HANDLER_DRIVE_H

#define _GNU_SOURCE

#include "handler_drive.h"

#include <errno.h> /* errno                                                */

#include "handler_utils.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
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

int handler_drive(const HttpRequest *req, HttpResponse *resp)
{
    /* ---------- 1. extract & decode the ?path query parameter ---------------- */

    const char *qmark = strchr(req->path, '?');                 /* position of '?' */
    char enc_path[HTTP_RECEIVE_BUFFER_LEN] = "/";               /* default = root  */
    if(qmark &&                                                 /* query present ? */
       sscanf(qmark, "?path=%" PATH_MAX_STR "s", enc_path) < 1) /* copy into buf   */
        strcpy(enc_path, "/");                                  /* sscanf failed   */

    char path[HTTP_RECEIVE_BUFFER_LEN];             /* decoded result  */
    if(url_decode(enc_path, path, sizeof path) < 0) /* may overflow    */
        send_400(resp);
    // return 0; /* early abort     */

    /* ---------- 2. basic sanitisation --------------------------------------- */
    if(strstr(path, "..")) /* path traversal? */
        send_400(resp);    /* reject          */

    /* ---------- 3. build absolute filesystem path --------------------------- */
    char full[FULLPATH_MAX];                              /* "www" + path    */
    int len = snprintf(full, sizeof full, "www%s", path); /* join components */
    if(len < 0 || len >= (int)sizeof full)                /* buffer overflow */
        send_400(resp);

    /* ---------- 4. open the target directory ------------------------------- */
    log_info("Opening dir: %s", full); /* trace           */
    DIR *dir = opendir(full);          /* try to open     */
    if(!dir)                           /* failure?        */
    {
        log_error("drive: opendir failed", strerror(errno)); /* reason in log   */
        resp->status_code = 404;                             /* HTTP Not Found  */
        resp->status_text = "Not Found";
        resp->content_type = "application/json";
        resp->body = strdup("{\"error\":\"not found\"}"); /* tiny payload    */
        resp->body_length = strlen(resp->body);
        return 0; /* done            */
    }

    /* ---------- 5. iterate entries and build JSON --------------------------- */
    cJSON *root = cJSON_CreateObject();                   /* { }             */
    cJSON *items = cJSON_AddArrayToObject(root, "items"); /* "items": [ ]    */
    cJSON_AddStringToObject(root, "path", path);          /* echo request    */

    struct dirent *ent;                 /* iteration var   */
    while((ent = readdir(dir)) != NULL) /* loop dirents    */
    {
        const char *name = ent->d_name;               /* entry name      */
        if(!strcmp(name, ".") || !strcmp(name, "..")) /* skip dots       */
            continue;

        cJSON *obj = cJSON_CreateObject();          /* { } per entry   */
        cJSON_AddStringToObject(obj, "name", name); /* "name": "foo"   */

        /* ---- decide file vs directory ------------------------------------ */
        char child[CHILDFULL_MAX]; /* full child path */
        int l = snprintf(child, sizeof child, "%s/%s", full, name);
        if(l < 0 || l >= (int)sizeof child) /* overflow guard  */
        {
            log_error("drive: child path overflow", name);
            cJSON_Delete(obj); /* discard entry   */
            continue;
        }

        struct stat st;                                  /* stat buffer     */
        if(stat(child, &st) == 0 && S_ISDIR(st.st_mode)) /* directory?      */
            cJSON_AddStringToObject(obj, "type", "directory");
        else /* else file       */
            cJSON_AddStringToObject(obj, "type", "file");

        cJSON_AddItemToArray(items, obj); /* push to "items" */
    }
    closedir(dir); /* release handle  */

    /* ---------- 6. serialise & populate HttpResponse ----------------------- */

    char *json = cJSON_PrintUnformatted(root); /* compact string  */
    cJSON_Delete(root);                        /* free DOM        */

    resp->status_code = 200; /* HTTP OK         */
    resp->status_text = "OK";
    resp->content_type = "application/json";
    resp->body = json; /* transfer owner  */
    resp->body_length = strlen(json);

    return 0; /* success         */
}

#endif /* SERVER_HANDLER_DRIVE_H */
