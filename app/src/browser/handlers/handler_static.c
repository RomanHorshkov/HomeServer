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
/* None */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int handler_static(const HttpRequest *req, HttpResponse *resp)
{
    int status = STATUS_FAILURE;                       /* overall handler status */
    char requested_file_path[HTTP_MAX_PATH_LEN] = {0}; /* filesystem path buffer */
    const char *rel_path = NULL;                       /* relative path to serve */
    FILE *f = NULL;                                    /* file handle */
    char *body = NULL;                                 /* buffer for file contents */
    long size = 0;                                     /* size of the file */

    /* Validate input pointers */
    if(req && resp)
    {
        /* Map "/" (home) to the SPA shell at views/index.html */
        if(strcmp(req->path, URI_HOME) == 0)
        {
            rel_path = "views/index.html";
        }
        /* Strip leading '/' from other absolute paths */
        else if(req->path[0] == '/' && req->path[1] != '\0')
        {
            rel_path = req->path + 1;
        }
        /* Strip leading "./" if present */
        else if(req->path[0] == '.' && req->path[1] == '/')
        {
            rel_path = req->path + 2;
        }
        /* Use the path as-is otherwise */
        else
        {
            rel_path = req->path;
        }

        /* Ensure the relative path fits into our buffer */
        if(strlen(rel_path) + 1 <= sizeof(requested_file_path))
        {
            /* Copy the relative path into requested_file_path */
            snprintf(requested_file_path, sizeof(requested_file_path), "%s", rel_path);

            /* Open the file in binary mode */
            f = fopen(requested_file_path, "rb");
            if(f)
            {
                /* Seek to end to determine file size */
                if(fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0)
                {
                    /* Rewind to beginning before reading */
                    rewind(f);

                    /* Allocate buffer to hold file contents */
                    body = malloc((size_t)size);
                    if(body &&
                       /* Read the entire file into the buffer */
                       fread(body, 1, (size_t)size, f) == (size_t)size)
                    {
                        /* Populate the HttpResponse structure */
                        resp->status_code = 200;
                        resp->status_text = "OK";
                        resp->content_type = guess_mime_type(requested_file_path);
                        resp->body = body;
                        resp->body_length = (size_t)size;
                        status = STATUS_SUCCESS;
                    }
                }
                /* Close file handle */
                fclose(f);
            }
        }
    }

    /* On failure, free any allocated buffer and send 404 */
    if(status != STATUS_SUCCESS)
    {
        if(body)
        {
            free(body);
        }
        send_404(resp);
    }

    /* Single exit point */
    return status;
}
