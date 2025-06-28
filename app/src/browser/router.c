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

#include <ctype.h>
#include <dirent.h>    // opendir, readdir, closedir
#include <errno.h>     // errno
#include <stdio.h>     // snprintf
#include <stdlib.h>    // malloc, free
#include <string.h>    // strcmp, strncmp, strrchr, strlen
#include <sys/stat.h>  // stat, S_ISDIR, S_ISREG
#include <unistd.h>    // access

#include "cJSON.h"
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

static void dir_to_json(const char *dirpath, cJSON *json_obj);

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

/**
 * @brief Routing table entry.
 */
typedef struct
{
    const char *path;        /* Path */
    size_t path_len;         /* Length of the path for optimization */
    route_handler_t handler; /* Handler function */
} route_t;

/****************************************************************************
 * ROUTING TABLE
 ****************************************************************************
 * Only API endpoints are explicitly routed here.
 */
static const route_t routes[] = {
    /* API endpoints */
    {"/api/whoami", 11, handler_whoami},
    {"/api/expenses", 13, handler_expenses},
    {"/api/drive", 10, handler_drive},
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
        if(generate_routes() != STATUS_SUCCESS)
        {
            log_error("[router]: Failed to generate routes", "");
        }
        initialized = 1;
    }

    if(!request || !response)
    {
        log_error("[router]: handle_request: invalid arguments", "");
    }

    /* Try API routes first */
    else
    {
        for(size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i)
        {
            if(strncmp(request->path, routes[i].path, routes[i].path_len) == 0)
            {
                return routes[i].handler(request, response);
            }
        }
    }

    /* Otherwise static handler */
    switch(request->method)
    {
        case HTTP_METHOD_GET:

            /* Delegate all static file requests to handler_static */
            return handler_static(request, response);

            break;

        case HTTP_METHOD_PUT:
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

static void dir_to_json(const char *dirpath, cJSON *json_obj)
{
    DIR *dir = opendir(dirpath);
    if(!dir)
    {
        fprintf(stderr, "Failed to open dir: %s (%s)\n", dirpath, strerror(errno));
        return;
    }
    struct dirent *entry;
    char path[1024];

    while((entry = readdir(dir)) != NULL)
    {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        snprintf(path, sizeof(path), "%s/%s", dirpath, entry->d_name);
        struct stat st;
        if(stat(path, &st) == -1) continue;

        if(S_ISDIR(st.st_mode))
        {
            cJSON *subdir = cJSON_CreateObject();
            dir_to_json(path, subdir);
            cJSON_AddItemToObject(json_obj, entry->d_name, subdir);
        }
        else if(S_ISREG(st.st_mode))
        {
            cJSON_AddStringToObject(json_obj, entry->d_name, path);
        }
    }
    closedir(dir);
}

static int generate_routes(void)
{
    cJSON *root = cJSON_CreateObject();
    dir_to_json(".", root);

    char *json_str = cJSON_Print(root);
    if(json_str)
    {
        FILE *f = fopen("map.json", "w");
        if(f)
        {
            fputs(json_str, f);
            fclose(f);
        }
        free(json_str);
    }
    cJSON_Delete(root);
    return 0;
}
