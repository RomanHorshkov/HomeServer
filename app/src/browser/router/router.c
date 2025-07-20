#define _GNU_SOURCE
/**
 * @file router.c
 * @brief HTTP request router implementation.
 *
 * Dispatches incoming requests to handlers for static files, APIs, or dynamic
 * content. Provides unified 404/405 responses when needed.
 *
 * Usage:
 *   router_handle_request(req, resp);
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

#include "cJSON.h"
#include "handlers.h"
#include "logger.h"
#include "route_register.h"

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */

/* Parsed copy of routes.json */
static char **api_paths = NULL; /* array of char* */
static size_t api_count = 0;

static char **view_paths = NULL;
static size_t view_count = 0;

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

static int router_cross_check(void);

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
static int generate_json_map(void);

static int router_load_routes_json(void);

static int has_dot_extension(const char *path);

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int router_init(void)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* parse the JSON into api_paths[] + view_paths[] */
    if(router_load_routes_json() != STATUS_SUCCESS)
    {
        log_error("[router] router_load_routes_json");
    }

    /* verify JSON handlers are in sync */
    else if(router_cross_check() != STATUS_SUCCESS)
    {
        log_error("[router] router_cross_check");
    }

    else if(generate_json_map() != STATUS_SUCCESS)
    {
        log_error("[router] generate_json_map");
    }

    else
    {
        res = STATUS_SUCCESS;
    }

    return res;
}

int router_handle_request(const HttpRequest *request, HttpResponse *response)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Check input validity */
    if(!request || !response)
    {
        log_error("[router]: router_handle_request: invalid arguments", "");
    }

    /* Try API routes first */
    else if(strncmp(request->path, "/api/", 5) == 0)
    {
#ifdef DEBUG_MODE
        log_info("[router] API route detected, calling handler");
#endif
        res = call_api_handler(request, response);
    }

    /* Static files (dot-extension) */
    else if(has_dot_extension(request->path))
    {
#ifdef DEBUG_MODE
        log_info("[router] dot with extension detected, calling handler_static");
#endif
        res = handler_static(request, response);
    }

    else
    {
        /* Try Views */
        for(size_t i = 0; i < view_count; ++i)
        {
            if(strcmp(request->path, view_paths[i]) == 0)
            {
#ifdef DEBUG_MODE
                log_info("[router] view found, calling handler_static");
#endif
                /* serve HTML/JS/CSS */
                res = handler_static(request, response);
            }
        }

        if(res != STATUS_SUCCESS)
        {
            log_error("[router] Fallback to / from %s", request->path);

            /* SPA fallback (serve homepage/entrypoint) */
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

static int call_api_handler(const HttpRequest *request, HttpResponse *response)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    size_t count;
    const route_t *table = router_get_table(&count);

    for(size_t i = 0; i < count; ++i)
    {
#ifdef DEBUG_MODE
        log_info("[router] api path %s, table path %s", request->path, table[i].path);
#endif
        if(strncmp(request->path, table[i].path, table[i].path_len) == 0)
        {
#ifdef DEBUG_MODE
            log_info("[router] YES, calling handler");
#endif
            res = table[i].handler(request, response);
            break;
        }
#ifdef DEBUG_MODE
        log_info("[router] NO");
#endif
    }

    return res;
}

static int generate_json_map(void)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Create the root JSON object */
    cJSON *root = cJSON_CreateObject();

    /* Populate the JSON object with the directory structure */
    dir_to_json(".", root);

    /* Convert the JSON object to a string */
    char *json_str = cJSON_Print(root);
    if(json_str)
    {
        /* Write the JSON string to the map.json file */
        FILE *f = fopen(ROUTER_MAP_JSON, "w");
        if(f)
        {
            fputs(json_str, f);
            fclose(f);
            /* Set success if file was written */
            res = STATUS_SUCCESS;
        }
        else
        {
            log_error("[router]: generate_json_map: Failed to open map.json for writing",
                      strerror(errno));
        }
        free(json_str);
    }

    /* Free the JSON object */
    cJSON_Delete(root);

    return res;
}

static int router_cross_check(void)
{
    int res = STATUS_SUCCESS;
    size_t count;
    const route_t *table = router_get_table(&count);

#ifdef DEBUG_MODE
    log_info("[router] cross_check: starting (api_count=%zu, handler_count=%zu)", api_count, count);
#endif

    for(size_t i = 0; i < api_count; ++i)
    {
#ifdef DEBUG_MODE
        log_info("[router] cross_check: checking API route '%s'", api_paths[i]);
#endif
        int found = 0;
        for(size_t j = 0; j < count; ++j)
        {
#ifdef DEBUG_MODE
            log_info("[router] cross_check:   comparing against handler '%s'", table[j].path);
#endif
            if(strcmp(api_paths[i], table[j].path) == 0)
            {
                found = 1;
#ifdef DEBUG_MODE
                log_info("[router] cross_check:   ✓ match found for '%s'", api_paths[i]);
#endif
                break;
            }
        }

        if(!found)
        {
#ifdef DEBUG_MODE
            log_info("[router] cross_check:   ✗ no handler registered for '%s'", api_paths[i]);
#endif
            fprintf(stderr,
                    "[router] WARNING: '%s' listed in routes.json but no handler registered\n",
                    api_paths[i]);
            res = STATUS_FAILURE;
        }
    }

#ifdef DEBUG_MODE
    log_info("[router] cross_check: done (returning %s)",
             res == STATUS_SUCCESS ? "STATUS_SUCCESS" : "STATUS_FAILURE");
#endif

    return res;
}

static void dir_to_json(const char *dirpath, cJSON *json_obj)
{
    /* Open the directory specified by dirpath */
    DIR *dir = opendir(dirpath);
    if(!dir)
    {
        fprintf(stderr, "Failed to open dir: %s (%s)\n", dirpath, strerror(errno));
        return;
    }

    const struct dirent *entry;
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

static int router_load_routes_json(void)
{
    /* Result variable */
    int res = STATUS_FAILURE;
    FILE *f = fopen(ROUTER_ROUTES_JSON, "rb");
    if(!f)
    {
        perror(ROUTER_ROUTES_JSON);
        return res;
    }

    /* Read entire file into buffer */
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);

    char *buf = malloc(len + 1);
    if(!buf)
    {
        perror("malloc");
        fclose(f);
        return res;
    }
    if(fread(buf, 1, len, f) != (size_t)len)
    {
        perror("fread");
        free(buf);
        fclose(f);
        return res;
    }
    buf[len] = '\0';
    fclose(f);

#ifdef DEBUG_MODE
    log_info("[router] loaded %ld bytes from %s", len, ROUTER_ROUTES_JSON);
#endif

    /* Parse JSON */
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if(!root)
    {
        fprintf(stderr, "[router] JSON parse error in %s\n", ROUTER_ROUTES_JSON);
        return res;
    }

    /* ---- APIs ---- */
    cJSON *apis = cJSON_GetObjectItem(root, "apis");
    api_count = (size_t)cJSON_GetArraySize(apis);
    api_paths = calloc(api_count, sizeof(*api_paths));
    if(!api_paths)
    {
        perror("calloc");
        cJSON_Delete(root);
        return res;
    }

    for(size_t i = 0; i < api_count; ++i)
    {
        /* duplicate the string so it remains valid after cJSON_Delete */
        const char *s = cJSON_GetArrayItem(apis, (int)i)->valuestring;
        api_paths[i] = strdup(s);
        if(!api_paths[i])
        {
            perror("strdup");
            /* fall through to cleanup */
            break;
        }
#ifdef DEBUG_MODE
        log_info("[router] api_paths[%zu] = %s", i, api_paths[i]);
#endif
    }

    /* ---- Views ---- */
    cJSON *views = cJSON_GetObjectItem(root, "views");
    view_count = (size_t)cJSON_GetArraySize(views);
    view_paths = calloc(view_count, sizeof(*view_paths));
    if(!view_paths)
    {
        perror("calloc");
        cJSON_Delete(root);
        return res;
    }

    for(size_t i = 0; i < view_count; ++i)
    {
        /* same deep copy for views */
        const char *s = cJSON_GetArrayItem(views, (int)i)->valuestring;
        view_paths[i] = strdup(s);
        if(!view_paths[i])
        {
            perror("strdup");
            /* fall through to cleanup */
            break;
        }
#ifdef DEBUG_MODE
        log_info("[router] view_paths[%zu] = %s", i, view_paths[i]);
#endif
    }

    /* all good: free JSON tree, old pointers are now owned by us */
    cJSON_Delete(root);
    res = STATUS_SUCCESS;
    return res;
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
