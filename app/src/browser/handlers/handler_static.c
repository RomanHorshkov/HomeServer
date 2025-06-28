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
#include "handler_static.h"

#include <stdio.h>  /* fopen, fseek, ftell, fread, fclose */
#include <stdlib.h> /* malloc, free */

#include "handler_utils.h"
#include "server_settings.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
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

int handler_static(const HttpRequest *req, HttpResponse *resp)
{
    int res = STATUS_FAILURE;
    char file_path[HTTP_MAX_PATH_LEN];
    FILE *file = NULL;
    char *buffer = NULL;

    // Home page mapping (configurable)
    const char *rel_path =
        (strcmp(req->path, "/") == 0) ? "views/index.html" : req->path + (req->path[0] == '/');
    if(strlen(rel_path) >= sizeof(file_path))
    {
        log_error("static_page: requested path too long: %s", req->path);
        send_404(resp);
        return res;
    }
    snprintf(file_path, sizeof(file_path), "%s", rel_path);

    file = fopen(file_path, "rb");
    if(!file)
    {
        log_error("[handler static]: open failed %s: %s", file_path, strerror(errno));
        send_404(resp);
        return res;
    }

    if(fseek(file, 0, SEEK_END) != 0)
    {
        log_error("[handler static]: fseek to end failed %s: %s", file_path, strerror(errno));
        fclose(file);
        send_404(resp);
        return res;
    }
    long file_size = ftell(file);
    if(file_size <= 0)
    {
        log_error("[handler static]: invalid or empty file %s", file_path);
        fclose(file);
        send_404(resp);
        return res;
    }
    rewind(file);

    buffer = malloc((size_t)file_size);
    if(!buffer)
    {
        log_error("[handler static]: malloc failed: %s", strerror(errno));
        fclose(file);
        send_404(resp);
        return res;
    }

    size_t total_read = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);

    if(total_read != (size_t)file_size)
    {
        log_error("[handler static]: fread failed for %s", file_path);
        free(buffer);
        send_404(resp);
        return res;
    }

    resp->status_code = 200;
    resp->status_text = "OK";
    resp->content_type = guess_mime_type(file_path);
    resp->body = buffer;
    resp->body_length = (size_t)file_size;

    return STATUS_SUCCESS;
}
