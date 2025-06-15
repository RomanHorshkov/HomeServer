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
#define PATH_MAX 4096
#define STATIC_ROOT "www"

#define INDEX_PAGE STATIC_ROOT "/index.html"
#define WHOAMI_PAGE STATIC_ROOT "/whoami.html"
#define DYNAMIC_PAGE STATIC_ROOT "/dynamic.html"
#define STYLE_PAGE STATIC_ROOT "/style.css"

#define URI_HOME "/"
#define URI_HOME_ALIAS "/home"
#define URI_STYLE "/style.css"
#define URI_WHOAMI "/whoami"
// #define URI_WHOAMI_API "/api/whoami"
#define URI_DYNAMIC "/dynamic"
#define URI_EXPENSES_PAGE "/expenses"
// #define URI_EXPENSES_MONTHS "/api/expenses/months"
#define URI_IMAGES_PREFIX "/images/"
#define URI_ASSETS_PREFIX "/assets/"
#define URI_CSS_PREFIX "/css/"
#define URI_JS_PREFIX "/js/"

#define CONTENT_HTML "text/html"
#define CONTENT_CSS "text/css"
#define CONTENT_JS "application/javascript"
#define CONTENT_JSON "application/json"
#define CONTENT_JPEG "image/jpeg"
#define CONTENT_PNG "image/png"
#define CONTENT_SVG "image/svg+xml"
#define CONTENT_GIF "image/gif"

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
    char file_path[PATH_MAX];

    // Prevent directory traversal
    if(strstr(req->path, ".."))
    {
        log_error("static_page: directory traversal attempt: %s", req->path);
        send_404(response);
        return res;
    }

    // Special case: "/" should serve index.html
    if(strcmp(req->path, URI_HOME) == 0)
    {
        snprintf(file_path, sizeof(file_path), "%s", INDEX_PAGE);
    }
    else if(strcmp(req->path, URI_HOME_ALIAS) == 0)
    {
        snprintf(file_path, sizeof(file_path), "%s", INDEX_PAGE);
    }
    else if(strcmp(req->path, URI_STYLE) == 0)
    {
        snprintf(file_path, sizeof(file_path), "%s", STYLE_PAGE);
    }
    else if(strcmp(req->path, URI_WHOAMI) == 0)
    {
        snprintf(file_path, sizeof(file_path), "%s", WHOAMI_PAGE);
    }
    else if(strcmp(req->path, URI_DYNAMIC) == 0)
    {
        snprintf(file_path, sizeof(file_path), "%s", DYNAMIC_PAGE);
    }
    

    log_info("[handler static]: file path %s", file_path);

    FILE *file = fopen(file_path, "rb");
    long file_size = 0;

    if(!file)
    {
        log_error("static_page: failed to open file %s: %s", file_path, strerror(errno));
        send_404(response);
        return res;
    }

    if(fseek(file, 0, SEEK_END) != 0)
    {
        log_error("static_page: fseek to end failed %s: %s", file_path, strerror(errno));
        fclose(file);
        send_404(response);
        return res;
    }

    file_size = ftell(file);
    if(file_size < 0)
    {
        log_error("static_page: ftell failed %s: %s", file_path, strerror(errno));
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
