#define _GNU_SOURCE
/**
 * @file router.c
 * @brief HTTP request router implementation.
 *
 * Dispatches incoming requests to APIs' handlers.
 * Provides unified 404/405 responses when needed.
 *
 * Exit Codes:
 *   STATUS_SUCCESS  (0)
 *   STATUS_FAILURE  (1)
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑05‑11
 * (c) 2025
 */

#include "router.h"

#include <ctype.h>
#include <dirent.h>    // opendir, readdir, closedir
#include <errno.h>     // errno
#include <stdio.h>     // snprintf
#include <stdlib.h>    // malloc, free
#include <string.h>    // strcmp, strncmp, strrchr, strlen
#include <sys/stat.h>  // stat, S_ISDIR, S_ISREG
#include <unistd.h>    // access

#include "handlers_interface.h"
#include "handlers_int.h"
#include "route_register.h"
#include <emlog.h>

#define LOG_TAG "router"

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */

/* Parsed copy of routes.json */
// static char **api_paths = NULL; /* array of char* */
// static size_t api_count = 0;

// static char **view_paths = NULL;
// static size_t view_count = 0;

/* Existing vector of handlers (populated by constructors) */
extern route_t *vec;
extern size_t used;

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTION PROTOTYPES
 ****************************************************************************
 */

static int call_api_handler(const HttpRequest *request, HttpResponse *response);

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int router_handle_request(const HttpRequest *request, HttpResponse *response)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Check input validity */
    if(!request || !response)
    {
        EML_ERROR(LOG_TAG, "[router]: router_handle_request: invalid arguments");
    }

    /* Try API routes first */
    else if(strncmp(request->path, "/api/", 5) == 0)
    {
#ifdef DEBUG_MODE
        EML_INFO(LOG_TAG, "[router] API request detected %s, searching handler", request->path);
#endif
        res = call_api_handler(request, response);
    }

    else
    {
        EML_ERROR(LOG_TAG, "[router] Fallback to / from %s", request->path);

        /* SPA fallback (serve homepage/entrypoint) */
        HttpRequest *copy_req = calloc(1, sizeof(HttpRequest));
        *copy_req = *request;  // shallow copy all fields
        strncpy(copy_req->path, "/", sizeof(copy_req->path));
        res = handler_static(copy_req, response);

        free(copy_req);

        /* free the respose mallocated body */
        // free(response->body);
    }

    return res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int call_api_handler(const HttpRequest *request, HttpResponse *response)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    size_t count = 0;
    const route_t *table = router_get_table(&count);

    /* Check if any api have been registered */
    if(count <= 0)
    {
        EML_ERROR(LOG_TAG, "[router] no entries in api table");
        if(response) send_404(response);
        return res;
    }

    /* Compare each table entry to request */
    for(size_t i = 0; i < count; ++i)
    {
        /* Check if any table entry corresponds to request */
        if(strncmp(request->path, table[i].path, table[i].path_len) == 0)
        {
            const char next = request->path[table[i].path_len];
            /* Check if next char is a / or \0, to avoid /api/whoami123 as ok */
            if(next == '\0' || next == '/')
            {
#ifdef DEBUG_MODE
                EML_INFO(LOG_TAG, "[router] api path %s, table path %s", request->path, table[i].path);
#endif
                res = table[i].handler(request, response);
            }

            else
            {
                EML_ERROR(LOG_TAG, "[router] wrong api request");
            }

            break;
        }
    }

    if(res != STATUS_SUCCESS && response && response->status_code == 0)
    {
        send_404(response);
    }

    return res;
}
