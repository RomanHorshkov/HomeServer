/**
 * @file router.c
 * @brief HTTP request router implementation
 *
 * This module dispatches incoming HTTP requests to appropriate handlers:
 *  - serving static files (HTML, CSS, images)
 *  - API endpoints (e.g., whoami, expenses)
 *  - dynamic content pages
 * It also provides unified 404 and 405 responses via dedicated helper functions.
 */

#include "router.h"

#include <stdio.h>   // snprintf
#include <string.h>  // strcmp, strncmp, strrchr, strlen
#include <unistd.h>  // access

#include "handlers.h"
#include "logger.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTION PROTOTYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * ROUTING TABLE STRUCTURES
 ****************************************************************************
 */

/**
 * @brief Handler function signature for all HTTP endpoints.
 *
 * @param req   Pointer to the parsed HttpRequest.
 * @param resp  Pointer to the HttpResponse to populate.
 * @retval 0    Success.
 * @retval -1   Failure.
 */
typedef int (*route_handler_t)(const HttpRequest *req, HttpResponse *resp);

typedef enum
{
    ROUTE_EXACT,
    ROUTE_PREFIX
} route_match_t;

/**
 * @brief Routing table entry.
 */
typedef struct
{
    http_method_t method; /* HTTP method for this route */
    const char *path;     /* Path */
    size_t path_len;      /* Length of the path for optimization */
    route_match_t match_type;
    route_handler_t handler; /* Handler function */
} route_t;

/****************************************************************************
 * ROUTING TABLE
 ****************************************************************************
 * Each entry maps a method and path/prefix to a handler.
 * For static files, handler_static will deduce the file path and MIME type.
 */
static const route_t routes[] = {
    /* Exact matches */
    {HTTP_METHOD_GET, "/", 1, ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/whoami.html", 12, ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/build_notes/index.html", 22, ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/dynamic.html", 13, ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/drive.html", 11, ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/expenses.html", 11, ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/style.css", 10, ROUTE_EXACT, handler_static},

    /* API endpoints */
    {HTTP_METHOD_GET, "/api/whoami", 12, ROUTE_EXACT, handler_whoami},
    {HTTP_METHOD_GET, "/api/expenses/settings.json", 28, ROUTE_EXACT, handler_expenses},
    {HTTP_METHOD_GET, "/api/expenses", 13, ROUTE_PREFIX, handler_expenses},
    {HTTP_METHOD_GET, "/api/drive", 11, ROUTE_EXACT, handler_drive},

    /* Prefix matches for static directories */
    {HTTP_METHOD_GET, "/images/", 8, ROUTE_PREFIX, handler_static},
    {HTTP_METHOD_GET, "/build_notes/", 13, ROUTE_PREFIX, handler_static},
    {HTTP_METHOD_GET, "/assets/", 8, ROUTE_PREFIX, handler_static},
    {HTTP_METHOD_GET, "/pages/", 7, ROUTE_PREFIX, handler_static},
};

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
int router_handle_request(const HttpRequest *request, HttpResponse *response)
{
    /* return variable */
    int res = STATUS_FAILURE;

    if(!request || !response)
    {
        log_error("[router]: handle_request: invalid arguments", "");
        // send_404(response);
        return -1;
    }

    switch(request->method)
    {
        case HTTP_METHOD_GET:
        case HTTP_METHOD_PUT:
            for(size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i)
            {
                if(routes[i].method != request->method) continue;

                if(routes[i].match_type == ROUTE_EXACT &&
                   strcmp(request->path, routes[i].path) == 0)
                {
                    return routes[i].handler(request, response);
                }
                else if(routes[i].match_type == ROUTE_PREFIX &&
                        strncmp(request->path, routes[i].path, routes[i].path_len) == 0)
                {
                    return routes[i].handler(request, response);
                }
            }
            /* No match case */
            res = STATUS_SUCCESS;
#ifdef DEBUG_MODE
            log_error("[router]: No match for: %s %s", http_method_to_string(request->method),
                     request->path);
#endif /* DEBUG_MODE */

            break;

        case HTTP_METHOD_POST:
        case HTTP_METHOD_DELETE:
        case HTTP_METHOD_UNKNOWN:
        default:
            log_error("[router]: invalid method", "");
            break;
    }

    return res;
}
