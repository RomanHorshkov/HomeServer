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

/**
 * @brief Converts a directory structure into a JSON object.
 *
 * This function recursively scans the given directory and adds its contents
 * (files and subdirectories) to the provided JSON object.
 *
 * @param dirpath   Path to the directory to scan.
 * @param json_obj  Pointer to the JSON object to populate.
 */
static void dir_to_json(const char *dirpath, cJSON *json_obj);

/**
 * @brief Generates the routing table dynamically by scanning the filesystem.
 *
 * This function creates a JSON file (`map.json`) that maps the directory
 * structure starting from the current directory. It uses `dir_to_json` to
 * recursively scan directories and files.
 *
 * @retval 0    Success.
 * @retval -1   Failure.
 */
static int generate_routes(void);

static int has_dot_extension(const char *path);

/**
 * @brief Handler function signature for all HTTP endpoints.
 *
 * @param req   Pointer to the parsed HttpRequest.
 * @param resp  Pointer to the HttpResponse to populate.
 * @retval 0    Success.
 * @retval -1   Failure.
 */
typedef int (*route_handler_t)(const HttpRequest *req, HttpResponse *resp);

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */

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
 * PRIVATE VARIABLES
 ****************************************************************************
 */
static const route_t routes[] = {
    /* API endpoints */
    {"/api/whoami", 11, handler_whoami},
    {"/api/expenses", 13, handler_expenses},
    {"/api/drive", 10, handler_drive},
};

/* Initialized flag for the router */
static int initialized = 0;

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
int router_handle_request(const HttpRequest *request, HttpResponse *response)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Generate the routing table if it does not exist */
    if(!initialized)
    {
        if(generate_routes() != STATUS_SUCCESS)
        {
            log_error("[router]: Failed to generate routes", "");
        }
        else
        {
            initialized = 1;
        }
    }

    /* Check input validity */
    if(!request || !response)
    {
        log_error("[router]: handle_request: invalid arguments", "");
    }

    // ---------- 1. Try API routes first ----------
    if(strncmp(request->path, "/api/", 5) == 0)
    {
        for(size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i)
        {
            if(strncmp(request->path, routes[i].path, routes[i].path_len) == 0)
            {
                res = routes[i].handler(request, response);
            }
        }
    }

    else
    {
        // ---------- 2. Static files (dot-extension) ----------
        if(has_dot_extension(request->path))
        {
            res = handler_static(request, response);
        }
        else
        {
            // ---------- 3. SPA fallback (serve homepage/entrypoint) ----------
            HttpRequest *copy_req = calloc(1, sizeof(HttpRequest));
            *copy_req = *request;  // shallow copy all fields
            strncpy(copy_req->path, "/", sizeof(copy_req->path));
            res = handler_static(copy_req, response);
            free(copy_req);
        }
    }

    return res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static void dir_to_json(const char *dirpath, cJSON *json_obj)
{
    /* Open the directory specified by dirpath */
    DIR *dir = opendir(dirpath);
    if(!dir)
    {
        fprintf(stderr, "Failed to open dir: %s (%s)\n", dirpath, strerror(errno));
        return;
    }

    struct dirent *entry;
    char path[1024];

    /* Iterate through all entries in the directory */
    while((entry = readdir(dir)) != NULL)
    {
        /* Skip special entries '.' and '..' */
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        /* Construct the full path for the current entry */
        snprintf(path, sizeof(path), "%s/%s", dirpath, entry->d_name);
        struct stat st;

        /* Get file status information */
        if(stat(path, &st) == -1) continue;

        if(S_ISDIR(st.st_mode))
        {
            /* If the entry is a directory, create a JSON object for it */
            cJSON *subdir = cJSON_CreateObject();
            dir_to_json(path, subdir); /* Recursively process the subdirectory */
            cJSON_AddItemToObject(json_obj, entry->d_name, subdir);
        }
        else if(S_ISREG(st.st_mode))
        {
            /* Trim leading "./" if present */
            const char *relpath = path;
            if(relpath[0] == '.' && relpath[1] == '/') relpath += 2;
            cJSON_AddStringToObject(json_obj, entry->d_name, relpath);
        }
    }

    /* Close the directory */
    closedir(dir);
}

static int generate_routes(void)
{
    /* Create the root JSON object */
    cJSON *root = cJSON_CreateObject();

    /* Populate the JSON object with the directory structure */
    dir_to_json(".", root);

    /* Convert the JSON object to a string */
    char *json_str = cJSON_Print(root);
    if(json_str)
    {
        /* Write the JSON string to the map.json file */
        FILE *f = fopen("map.json", "w");
        if(f)
        {
            fputs(json_str, f);
            fclose(f);
        }
        free(json_str);
    }

    /* Free the JSON object */
    cJSON_Delete(root);

    return 0;
}

static int has_dot_extension(const char *path)
{
    /* Find last '/' */
    const char *last_slash = strrchr(path, '/');
    const char *last_part = last_slash ? last_slash + 1 : path;

    /* Find last dot after the last slash */
    const char *dot = strrchr(last_part, '.');

    /* true if there's a dot and at least one char after */
    return (dot && dot[1]);
}
