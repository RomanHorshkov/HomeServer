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
#define PATH_MAX        4096
#define STATIC_ROOT     "www"

#define INDEX_PAGE      STATIC_ROOT "/index.html"
#define WHOAMI_PAGE     STATIC_ROOT "/whoami.html"
#define DYNAMIC_PAGE    STATIC_ROOT "/dynamic.html"
#define STYLE_PAGE      STATIC_ROOT "/style.css"

#define URI_HOME            "/"
#define URI_HOME_ALIAS      "/home"
#define URI_STYLE           "/style.css"
#define URI_WHOAMI          "/whoami"
#define URI_WHOAMI_API      "/api/whoami"
#define URI_DYNAMIC         "/dynamic"
#define URI_EXPENSES_PAGE   "/expenses"
#define URI_EXPENSES_MONTHS "/api/expenses/months"
#define URI_IMAGES_PREFIX   "/images/"
#define URI_ASSETS_PREFIX   "/assets/"
#define URI_CSS_PREFIX      "/css/"
#define URI_JS_PREFIX       "/js/"

#define CONTENT_HTML    "text/html"
#define CONTENT_CSS     "text/css"
#define CONTENT_JS      "application/javascript"
#define CONTENT_JSON    "application/json"
#define CONTENT_JPEG    "image/jpeg"
#define CONTENT_PNG     "image/png"
#define CONTENT_SVG     "image/svg+xml"
#define CONTENT_GIF     "image/gif"

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
 * @section Static Route Table
 ****************************************************************************/

typedef struct {
    const char *uri;
    const char *file_path;
    const char *content_type;
} StaticRoute;

static const StaticRoute static_routes[] = {
    { URI_HOME,            INDEX_PAGE,        CONTENT_HTML },
    { URI_HOME_ALIAS,      INDEX_PAGE,        CONTENT_HTML },
    { URI_STYLE,           STYLE_PAGE,        CONTENT_CSS  },
    { URI_WHOAMI,          WHOAMI_PAGE,       CONTENT_HTML },
    { URI_DYNAMIC,         DYNAMIC_PAGE,      CONTENT_HTML },
    { URI_EXPENSES_PAGE,   STATIC_ROOT "/expenses.html",      CONTENT_HTML },
    { URI_EXPENSES_MONTHS, STATIC_ROOT "/expenses_months.json", CONTENT_JSON },
    { URI_IMAGES_PREFIX,   STATIC_ROOT "/images/",           NULL }, // handled as prefix, content type guessed
    { URI_ASSETS_PREFIX,   STATIC_ROOT "/assets/",           NULL }, // handled as prefix, content type guessed
    { URI_CSS_PREFIX,      STATIC_ROOT "/css/",              CONTENT_CSS },
    { URI_JS_PREFIX,       STATIC_ROOT "/js/",               CONTENT_JS  },
};

#define STATIC_ROUTE_COUNT (sizeof(static_routes)/sizeof(static_routes[0]))

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int handler_static(const HttpRequest *req, HttpResponse *response)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    char file_path[HTTP_MAX_PATH_LEN+3];

    snprintf(file_path, sizeof(file_path), "www%s", req->path);
    
    /* Open file in binary mode */
    FILE *file = fopen(file_path, "rb");

    /* prepare filesize variable */
    long file_size = 0;

    if(!file)
    {
        log_error("static_page: failed to open file %s", strerror(errno), file_path);
    }

    /* Determine file size: seek to end */
    else if(fseek(file, 0, SEEK_END) != 0)
    {
        log_error("static_page: fseek to end failed %s", strerror(errno), file_path);
        fclose(file);
    }

    /* Check the file size */
    else if((file_size = ftell(file)) < 0)
    {
        log_error("static_page: ftell failed %s", strerror(errno), file_path);
        fclose(file);
    }

    /* Guard against empty files */
    else if(file_size == 0)
    {
        log_error("static_page: file is empty", file_path);
        fclose(file);
    }

    /* Reset file position to beginning */
    else if(fseek(file, 0, SEEK_SET) != 0)
    {
        log_error("static_page: fseek to start failed", strerror(errno));
        fclose(file);
    }

    /* Read file contents into response */
    else
    {
        /* Allocate buffer for file contents */
        char *buffer = (char *)malloc((size_t)file_size);
        if(!buffer)
        {
            log_error("static_page: malloc failed", strerror(errno));
            log_error("static_page: cannot read file", file_path);
            fclose(file);
        }

        /* Read all bytes */
        else
        {
            /* Read the file */
            size_t total_read = fread(buffer, 1, (size_t)file_size, file);

            /* Close file descriptor now that data is in memory */
            fclose(file);

            if(total_read != (size_t)file_size)
            {
                log_error("static_page: fread read fewer bytes than expected", file_path);
                free(buffer);
            }

            else
            {
                /* Guess MIME type from file extension */
                const char *mime = guess_mime_type(file_path);

                /* Populate HttpResponse */
                response->status_code = 200;
                response->status_text = "OK";
                response->content_type = mime;
                response->body = buffer;
                response->body_length = (size_t)file_size;

                /* set return value */
                res = STATUS_SUCCESS;
            }
        }
    }

    return res;
}

#endif /* SERVER_HANDLER_STATIC_H */
