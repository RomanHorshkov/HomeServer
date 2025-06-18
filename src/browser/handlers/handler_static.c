/**
 * @file static_page.c
 * @brief Reads static files from disk and populates HTTP responses.
 *
 * This module provides a single public function:
 *   - handler_static_page: open a file, read its contents, and fill
 *     in an HttpResponse with the appropriate headers and body.
 *
 * Detailed error logging is performed via logger.h on failures.
 */

#ifndef SERVER_HANDLER_STATIC_H
#define SERVER_HANDLER_STATIC_H

#include "handler_static.h"

#include <stdio.h>  /* fopen, fseek, ftell, fread, fclose */
#include <stdlib.h> /* malloc, free */

#include "handler_utils.h"
#include "server_settings.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
#define STATIC_ROOT "www"
#define URI_HOME "/"

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int handler_static(const HttpRequest *req, HttpResponse *response)
{
    int res = STATUS_FAILURE;
    char file_path[HTTP_MAX_PATH_LEN];

    /* Prevent directory traversal */
    if(strstr(req->path, ".."))
    {
        log_error("static_page: directory traversal attempt: %s", req->path);
        send_404(response);
        return res;
    }

    /* Map "/" to "www/index.html", otherwise map directly under www/ */
    if(strcmp(req->path, URI_HOME) == 0)
    {
        snprintf(file_path, sizeof(file_path), "%s/index.html", STATIC_ROOT);
    }
    else
    {
        /* Remove leading slash for correct path join */
        const char *rel_path = req->path[0] == '/' ? req->path + 1 : req->path;
        size_t root_len = strlen(STATIC_ROOT);
        size_t rel_len = strlen(rel_path);

        /* +1 for '/', +1 for '\0' */
        if(root_len + 1 + rel_len + 1 > sizeof(file_path))
        {
            log_error("static_page: requested path too long: %s", req->path);
            send_404(response);
            return res;
        }
        memcpy(file_path, STATIC_ROOT, root_len);
        file_path[root_len] = '/';
        memcpy(file_path + root_len + 1, rel_path, rel_len);
        file_path[root_len + 1 + rel_len] = '\0';

#ifdef DEBUG_MODE
        log_info("[handler static]: file path %s; opening...", file_path);
#endif /* DEBUG_MODE */
    }

    FILE *file = fopen(file_path, "rb");
    if(!file)
    {
        log_error("[handler static]: failed to open file %s: %s", file_path, strerror(errno));
        send_404(response);
        return res;
    }

    if(fseek(file, 0, SEEK_END) != 0)
    {
        log_error("[handler static]: fseek to end failed %s: %s", file_path, strerror(errno));
        fclose(file);
        send_404(response);
        return res;
    }

    long file_size = ftell(file);
    if(file_size < 0)
    {
        log_error("[handler static]: ftell failed %s: %s", file_path, strerror(errno));
        fclose(file);
        send_404(response);
        return res;
    }

    if(file_size == 0)
    {
        log_error("static_page: file is empty: %s", file_path);
        fclose(file);
        send_404(response);
        return res;
    }

    if(fseek(file, 0, SEEK_SET) != 0)
    {
        log_error("static_page: fseek to start failed %s: %s", file_path, strerror(errno));
        fclose(file);
        send_404(response);
        return res;
    }

    char *buffer = (char *)malloc((size_t)file_size);
    if(!buffer)
    {
        log_error("static_page: malloc failed: %s", strerror(errno));
        fclose(file);
        send_404(response);
        return res;
    }

    size_t total_read = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);

    if(total_read != (size_t)file_size)
    {
        log_error("static_page: fread read fewer bytes than expected: %s", file_path);
        free(buffer);
        send_404(response);
        return res;
    }

    const char *mime = guess_mime_type(file_path);

    response->status_code = 200;
    response->status_text = "OK";
    response->content_type = mime;
    response->body = buffer;
    response->body_length = (size_t)file_size;
    res = STATUS_SUCCESS;

    return res;
}

#endif /* SERVER_HANDLER_STATIC_H */
