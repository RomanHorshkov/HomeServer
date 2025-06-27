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
#include <dirent.h>  // opendir, readdir, closedir
#include <errno.h>   // errno
#include <stdio.h>   // snprintf
#include <stdlib.h>  // malloc, free
#include <string.h>  // strcmp, strncmp, strrchr, strlen
#include <unistd.h>  // access

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

/* Helper function for recursion: scan files and directories */
static void scan_files(const char *base, cJSON *files_obj)
{
    DIR *dir = opendir(base);
    if(!dir)
    {
        log_error("[router]: DEBUG - Failed to open directory: '%s' - %s", base, strerror(errno));
        return;
    }
    // log_info("[router]: DEBUG - Successfully opened directory: '%s'", base);

    struct dirent *entry;
    char path[512];
    int entry_count = 0;

    while((entry = readdir(dir)) != NULL)
    {
        entry_count++;
        // log_info("[router]: DEBUG - Found entry #%d: '%s' (type: %d)", entry_count,
        // entry->d_name,
        //          entry->d_type);

        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            // log_info("[router]: DEBUG - Skipping special directory: '%s'", entry->d_name);
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", base, entry->d_name);
        // log_info("[router]: DEBUG - Full path constructed: '%s'", path);

        if(entry->d_type == DT_DIR)
        {
            // log_info("[router]: DEBUG - Processing directory: '%s'", entry->d_name);
            cJSON *subdir = cJSON_CreateObject();
            if(!subdir)
            {
                log_error("[router]: DEBUG - Failed to create JSON object for directory: '%s'",
                          entry->d_name);
                continue;
            }
            scan_files(path, subdir);
            cJSON_AddItemToObject(files_obj, entry->d_name, subdir);
            // log_info("[router]: DEBUG - Added directory object for: '%s'", entry->d_name);
        }
        else if(entry->d_type == DT_REG)
        {
            // log_info("[router]: DEBUG - Processing regular file: '%s'", entry->d_name);
            cJSON_AddStringToObject(files_obj, entry->d_name, path);
            // log_info("[router]: DEBUG - Added file string for: '%s' -> '%s'", entry->d_name,
            // path);
        }
        else
        {
            // log_info("[router]: DEBUG - Skipping unknown entry type %d: '%s'", entry->d_type,
            //          entry->d_name);
        }
    }

    closedir(dir);
}

static int generate_routes(void)
{
    /* Initialize return status to failure - will be set to success only if everything works */
    int res = STATUS_FAILURE;

    /* Create the main JSON object that will contain our complete filesystem map */
    cJSON *root = cJSON_CreateObject();
    if(!root)
    {
        log_error("[router]: DEBUG - Failed to create root JSON object", "");
        return res;
    }

    /* Recursively scan the entire current directory structure starting from "." (current directory)
     */
    /* This will create a nested JSON structure that mirrors the filesystem hierarchy */
    // log_info("[router]: DEBUG - About to call scan_files on current directory", "");
    scan_files(".", root);
    // log_info("[router]: DEBUG - Completed scan_files call", "");

    /* Debug: Print what we found in the root object */
    char *debug_json = cJSON_Print(root);
    if(debug_json)
    {
        // log_info("[router]: DEBUG - Root JSON structure: %s", debug_json);
        free(debug_json);
    }
    else
    {
        log_error("[router]: DEBUG - Failed to print root JSON for debugging", "");
    }

    /* Directly map all files without special handling */
    cJSON *views_obj = cJSON_GetObjectItem(root, "views");
    if(views_obj)
    {
        cJSON *file_item = NULL;
        cJSON_ArrayForEach(file_item, views_obj)
        {
            const char *filename = file_item->string;
            const char *original_path = cJSON_GetStringValue(file_item);
            if(filename && original_path)
            {
                cJSON_AddStringToObject(views_obj, filename, original_path); // Direct mapping
            }
        }
    }
    else
    {
        log_error("[router]: DEBUG - No views object found in root", "");
    }

    /* Convert the JSON object to a formatted string for writing to file */
    char *json_str = cJSON_Print(root);
    if(!json_str)
    {
        log_error("[router]: DEBUG - Failed to convert JSON to string", "");
        cJSON_Delete(root);
        return res;
    }
    /* Open the map.json file for writing (this will create or overwrite the file) */
    FILE *map_file = fopen("map.json", "w");
    if(!map_file)
    {
        /* If file couldn't be opened, log the error and clean up allocated memory */
        log_error("[router]: DEBUG - Failed to open map.json: %s", strerror(errno));
        cJSON_Delete(root);
        free(json_str);
        return res;
    }

    /* Write the JSON string to the file with a newline at the end */
    int write_result = fprintf(map_file, "%s\n", json_str);
    if(write_result < 0)
    {
        log_error("[router]: DEBUG - Failed to write to map.json", "");
        fclose(map_file);
        cJSON_Delete(root);
        free(json_str);
        return res;
    }
    log_info("[router]: DEBUG - Successfully wrote %d characters to file", write_result);

    /* Close the file to ensure all data is written and resources are freed */
    fclose(map_file);

    /* Clean up allocated memory */
    cJSON_Delete(root); /* This recursively frees all nested JSON objects */
    free(json_str);     /* Free the JSON string buffer */

    /* If reached this point, everything worked successfully */
    res = STATUS_SUCCESS;
    return res;
}
