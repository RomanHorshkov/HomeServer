#define _GNU_SOURCE
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

#include <dirent.h>  // opendir, readdir, closedir
#include <errno.h>   // errno
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

/*
 * @brief Generates the routing table dynamically by scanning the /views directory.
 * This function creates a JSON file with all available static pages and API endpoints.
 *
 * @retval 0    Success.
 * @retval -1   Failure.
 */
static int generate_routes(void);

/* Helper function to recursively scan a directory and write entries to the JSON file */
static void write_dir_json(FILE *routes_file, const char *base_path, int *first);

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
 * Only API endpoints are explicitly routed here.
 */
static const route_t routes[] = {
    /* API endpoints */
    {HTTP_METHOD_GET, "/api/whoami", 12, ROUTE_EXACT, handler_whoami},
    {HTTP_METHOD_GET, "/api/expenses", 13, ROUTE_PREFIX, handler_expenses},
    {HTTP_METHOD_GET, "/api/drive", 11, ROUTE_EXACT, handler_drive},
};

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
int router_handle_request(const HttpRequest *request, HttpResponse *response)
{
    int res = STATUS_FAILURE;

    static int initialized = 0;

    if(!initialized)
    {
        /* Generate the routing table if it does not exist */
        if(generate_routes() == STATUS_FAILURE)
        {
            log_error("[router]: Failed to generate routes", "");
            return res;
        }
        initialized = 1;
    }

    if(!request || !response)
    {
        log_error("[router]: handle_request: invalid arguments", "");
        return res;
    }

    switch(request->method)
    {
        case HTTP_METHOD_GET:
        case HTTP_METHOD_PUT:
            /* Try API routes first */
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

            /* Delegate all static file requests to handler_static */
            return handler_static(request, response);

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


/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int generate_routes(void)
{
    int res = STATUS_FAILURE;

    DIR *dir = opendir(".");
    if(!dir)
    {
        log_error("[router]: generate routes opendir", strerror(errno));
        return res;
    }
    closedir(dir);

    FILE *routes_file = fopen("routes.json", "w");
    if(!routes_file)
    {
        log_error("[router]: generate routes fopen", strerror(errno));
        return res;
    }

    res = STATUS_SUCCESS;

    fprintf(routes_file, "{\n  \"pages\": {\n");

    int first = 1;
    write_dir_json(routes_file, ".", &first);

    fprintf(routes_file, "\n  },\n");

    // Hardcoded API endpoints (expand as needed)
    fprintf(routes_file, "  \"apis\": [\n");
    fprintf(routes_file, "    { \"method\": \"GET\", \"path\": \"/api/whoami\" },\n");
    fprintf(routes_file, "    { \"method\": \"GET\", \"path\": \"/api/expenses\" },\n");
    fprintf(routes_file, "    { \"method\": \"PUT\", \"path\": \"/api/expenses\" },\n");
    fprintf(routes_file, "    { \"method\": \"GET\", \"path\": \"/api/drive\" }\n");
    fprintf(routes_file, "  ]\n");

    fprintf(routes_file, "}\n");

    fclose(routes_file);
    return res;
}

static void write_dir_json(FILE *routes_file, const char *base_path, int *first)
{
    DIR *dir = opendir(base_path);
    if (!dir) return;

    struct dirent *entry;
    char path[512];
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s", base_path, entry->d_name);

        if (entry->d_type == DT_DIR)
        {
            if (!*first) fprintf(routes_file, ",\n");
            *first = 0;
            fprintf(routes_file, "    \"%s\": { \"type\": \"dir\", \"path\": \"%s\" }", entry->d_name, path);
            // Recursively scan subdirectory
            write_dir_json(routes_file, path, first);
        }
        else if (entry->d_type == DT_REG)
        {
            if (!*first) fprintf(routes_file, ",\n");
            *first = 0;
            fprintf(routes_file, "    \"%s\": { \"type\": \"file\", \"path\": \"%s\" }", entry->d_name, path);
        }
    }
    closedir(dir);
}
