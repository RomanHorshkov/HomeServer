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

// #include "handlers_interface.h"
#include "route_register.h"
#include "emlog.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "srv_router"

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

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int router_handle_request(const http_request_t *request, HttpResponse *response)
{
    /* Check input request validity */
    if(!request || !request->message_complete)
    {
        EML_ERROR(LOG_TAG, "_handle_request: invalid request");
        return STATUS_FAILURE;
    }

    /* Check input response validity */
    if(!response)
    {
        EML_ERROR(LOG_TAG, "_handle_request: invalid response");
        return STATUS_FAILURE;
    }

    int res = STATUS_FAILURE;
    /* Try API routes first */
    if(memcmp(&request->path.p, "/api/", 5) == 0)
    {
#ifdef MODE_DEBUG
        EML_INFO(LOG_TAG, "_handle_request: API request detected, searching handler");
#endif
        // res = call_api_handler(request, response);
    }

    else
    {
        EML_ERROR(LOG_TAG, "[router] Fallback to / from %.*s",
                  (int)request->path.n, request->path.p ? request->path.p : "");

        /* SPA fallback (serve homepage/entrypoint) */
        Http_request_t *copy_req = calloc(1, sizeof(Http_request_t));
        *copy_req = *request;  // shallow copy all fields
        copy_req->path.p = "/";
        copy_req->path.n = 1;
        // res = handler_static(copy_req, response);

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

// static int call_api_handler(const Http_request_t *request, HttpResponse *response)
// {
//     /* Return variable */
//     int res = STATUS_FAILURE;

//     size_t count = 0;
//     const route_t *table = router_get_table(&count);

//     /* Check if any api have been registered */
//     if(count <= 0)
//     {
//         EML_ERROR(LOG_TAG, "[router] no entries in api table");
//         // if(response) send_404(response);
//         return res;
//     }

//     /* Compare each table entry to request */
//     for(size_t i = 0; i < count; ++i)
//     {
//         /* Check if any table entry corresponds to request */
//         if(sv_starts_with(&request->path, table[i].path, table[i].path_len))
//         {
//             char next = '\0';
//             if(request->path.p && request->path.n > table[i].path_len)
//             {
//                 next = request->path.p[table[i].path_len];
//             }
//             /* Check if next char is a / or \0, to avoid /api/whoami123 as ok */
//             if(next == '\0' || next == '/')
//             {
// #ifdef MODE_DEBUG
//                 EML_INFO(LOG_TAG, "[router] api path %.*s, table path %s",
//                          (int)request->path.n, request->path.p ? request->path.p : "", table[i].path);
// #endif
//                 res = table[i].handler(request, response);
//             }

//             else
//             {
//                 EML_ERROR(LOG_TAG, "[router] wrong api request");
//             }

//             break;
//         }
//     }

//     if(res != STATUS_SUCCESS && response && response->status_code == 0)
//     {
//         EML_ERROR(LOG_TAG, "[router] api handler not found for %.*s",
//                   (int)request->path.n, request->path.p ? request->path.p : "");
//         // send_404(response);
//     }

//     return res;
// }
