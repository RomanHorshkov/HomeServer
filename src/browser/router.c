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

/* --- Path and File Limits --- */
#define STATIC_ROOT "www" /* Root directory for all static assets */

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
 * @brief Route matching type.
 */
typedef enum
{
    ROUTE_EXACT,  ///< Exact path match
    ROUTE_PREFIX  ///< Prefix match (e.g., "/images/")
} route_match_t;

/**
 * @brief Handler function signature for all HTTP endpoints.
 *
 * @param req   Pointer to the parsed HttpRequest.
 * @param resp  Pointer to the HttpResponse to populate.
 * @retval 0    Success.
 * @retval -1   Failure.
 */
typedef int (*route_handler_t)(const HttpRequest *req, HttpResponse *resp);

/**
 * @brief Routing table entry.
 */
typedef struct
{
    http_method_t method;      ///< HTTP method for this route
    const char *path;          ///< Path or prefix
    route_match_t match_type;  ///< Exact or prefix match
    route_handler_t handler;   ///< Handler function
} route_t;

/****************************************************************************
 * ROUTING TABLE
 ****************************************************************************
 * Each entry maps a method and path/prefix to a handler.
 * For static files, handler_static will deduce the file path and MIME type.
 */
static const route_t routes[] = {

    /* Servicing */
    /* Exact matches for static files */
    {HTTP_METHOD_GET, "/", ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/home", ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/whoami.html", ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/build_notes/index.html", ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/dynamic.html", ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/expenses.html", ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/drive.html", ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/style.css", ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/assets/footer.html", ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/assets/header.html", ROUTE_EXACT, handler_static},
    {HTTP_METHOD_GET, "/assets/header.js", ROUTE_EXACT, handler_static},

    /* API endpoints */
    {HTTP_METHOD_GET, "/api/whoami", ROUTE_EXACT, handler_whoami},
    {HTTP_METHOD_GET, "/api/expenses", ROUTE_EXACT, handler_expenses},
    {HTTP_METHOD_PUT, "/api/expenses", ROUTE_EXACT, handler_expenses},
    {HTTP_METHOD_GET, "/api/drive", ROUTE_PREFIX, handler_drive},

    /* Prefix matches for static directories */
    {HTTP_METHOD_GET, "/images", ROUTE_PREFIX, handler_static},
    {HTTP_METHOD_GET, "/assets", ROUTE_PREFIX, handler_static},
    {HTTP_METHOD_GET, "/expenses/", ROUTE_PREFIX, handler_static},
    {HTTP_METHOD_GET, "/build_notes/", ROUTE_PREFIX, handler_static},

};

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

/**
 * @brief Route an HTTP request to the appropriate handler.
 *
 * Iterates through the routing table and dispatches the request to the first
 * matching handler. If no match is found, fills the response with a 404.
 *
 * @param request   Pointer to the parsed HttpRequest.
 * @param response  Pointer to the HttpResponse to populate.
 * @retval 0        Success.
 * @retval -1       No matching route (404).
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
        case HTTP_METHOD_GET: /* TO DO: check fall-through mechanism */
        case HTTP_METHOD_PUT:
            for(size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i)
            {
                if(routes[i].method != request->method)
                {
                    continue;
                }

                else if(routes[i].match_type == ROUTE_EXACT &&
                        strcmp(request->path, routes[i].path) == 0)
                {
                    return routes[i].handler(request, response);
                }

                else if(routes[i].match_type == ROUTE_PREFIX &&
                        strncmp(request->path, routes[i].path, strlen(routes[i].path)) == 0)
                {
                    return routes[i].handler(request, response);
                }

                else
                {
                    /*
                    TODO:
                    The iterations every time ends up here!!!
                    It is wasting a ton of time in checks!!! */
#ifdef DEBUG_MODE
                    // log_info("[router]: No match for: %s %s",
                    // http_method_to_string(request->method), request->path);
#endif /* DEBUG_MODE */
                    continue;
                }
            }
            /* No match case */
            res = STATUS_SUCCESS;
#ifdef DEBUG_MODE
            log_info("[router]: No match for: %s %s", http_method_to_string(request->method),
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
