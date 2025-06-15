/**
 * @file router.c
 * @brief HTTP request router implementation
 *
 * This module dispatches incoming HTTP GET requests to appropriate handlers:
 *  - serving static files (HTML, CSS, images)
 *  - the whoami API
 *  - dynamic content page
 * It also provides unified 404 and 405 responses via dedicated helper functions.
 */

#include "router.h"

#include <stdio.h>  /* snprintf */
#include <string.h> /* strcmp, strrchr, strlen */
#include <unistd.h> /* access */

#include "handlers.h"
#include "logger.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

/* --- Path and File Limits --- */
#define STATIC_ROOT "www" /* Root directory for all static assets */

/* --- Default Static File Paths --- */
#define INDEX_PAGE STATIC_ROOT "/index.html"
#define WHOAMI_PAGE STATIC_ROOT "/whoami.html"
#define DYNAMIC_PAGE STATIC_ROOT "/dynamic.html"
#define STYLE_PAGE STATIC_ROOT "/style.css"
#define EXPENSES_PAGE STATIC_ROOT "/expenses.html"
#define BUILD_NOTES_PAGE STATIC_ROOT "/build_notes/index.html"
#define BUILD_NOTES_DIR STATIC_ROOT "/build_notes/diagrams/"

/* --- URI Route Patterns --- */
#define URI_HOME "/"
#define URI_HOME_ALIAS "/home"
#define URI_STYLE "/style.css"
#define URI_ASSETS_PREFIX "/assets/"
#define URI_WHOAMI "/whoami"
#define URI_WHOAMI_API "/api/whoami"
#define URI_DYNAMIC "/dynamic"
#define URI_BUILD_NOTES_PREFIX "/build_notes"
#define URI_DRIVE_PREFIX "/drive"
#define URI_API_DRIVE_PREFIX "/api/drive"
#define URI_IMAGES_PREFIX "/images/"
#define URI_EXPENSES_PREFIX "/expenses/"
#define URI_EXPENSES_PAGE "/expenses"
#define URI_EXPENSES_MONTHS "/api/expenses/months"

/* --- Content Types --- */
#define CONTENT_HTML "text/html"
#define CONTENT_CSS "text/css"
#define CONTENT_MARKDOWN "text/markdown"
#define CONTENT_PUML "text/plain"
#define CONTENT_PNG "image/png"
#define CONTENT_JPEG "image/jpeg"
#define CONTENT_SVG "image/svg+xml"
#define CONTENT_GIF "image/gif"
#define CONTENT_JSON "application/json"
#define CONTENT_JS "application/javascript"
#define CONTENT_OCTET "application/octet-stream"

/* --- HTTP Methods --- */
#define HTTP_GET "GET"

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Serve a static file in response to a request.
 *
 * @param req         Parsed HTTP request structure.
 * @param res         HTTP response structure to populate.
 * @param file_path   Absolute or relative path to the static file.
 * @param content_type MIME type string to use in the response.
 * @return            0 on success, -1 on error (file not found, etc.).
 */
static int handle_static(const HttpRequest *req, HttpResponse *res, const char *file_path,
                         const char *content_type);

/**
 * @brief Handle the /api/whoami endpoint (returns server info as JSON).
 *
 * @param req         Parsed HTTP request structure.
 * @param res         HTTP response structure to populate.
 * @param unused1     Unused parameter (for handler signature compatibility).
 * @param unused2     Unused parameter (for handler signature compatibility).
 * @return            0 on success, -1 on error.
 */
static int handle_whoami_api(const HttpRequest *req, HttpResponse *res, const char *unused1,
                             const char *unused2);

/**
 * @brief Handle the /api/drive endpoint (returns directory listing as JSON).
 *
 * @param req         Parsed HTTP request structure.
 * @param res         HTTP response structure to populate.
 * @param unused1     Unused parameter (for handler signature compatibility).
 * @param unused2     Unused parameter (for handler signature compatibility).
 * @return            0 on success, -1 on error.
 */
static int handle_drive_api(const HttpRequest *req, HttpResponse *res, const char *unused1,
                            const char *unused2);

/**
 * @brief Handle the /api/expenses/months endpoint (returns months as JSON).
 *
 * @param req         Parsed HTTP request structure.
 * @param res         HTTP response structure to populate.
 * @param unused1     Unused parameter (for handler signature compatibility).
 * @param unused2     Unused parameter (for handler signature compatibility).
 * @return            0 on success, -1 on error.
 */
static int handle_expenses_months(const HttpRequest *req, HttpResponse *res, const char *unused1,
                                  const char *unused2);

/**
 * @brief Serve static images under the /images/ prefix.
 *
 * @param req         Parsed HTTP request structure.
 * @param res         HTTP response structure to populate.
 * @param unused1     Unused parameter.
 * @param unused2     Unused parameter.
 * @return            0 on success, -1 on error.
 */
static int handle_prefix_images(const HttpRequest *req, HttpResponse *res, const char *unused1,
                                const char *unused2);

/**
 * @brief Serve static assets under the /assets/ prefix.
 *
 * @param req         Parsed HTTP request structure.
 * @param res         HTTP response structure to populate.
 * @param unused1     Unused parameter.
 * @param unused2     Unused parameter.
 * @return            0 on success, -1 on error.
 */
static int handle_prefix_assets(const HttpRequest *req, HttpResponse *res, const char *unused1,
                                const char *unused2);

/**
 * @brief Serve build notes and files under the /build_notes prefix.
 *
 * @param req         Parsed HTTP request structure.
 * @param res         HTTP response structure to populate.
 * @param unused1     Unused parameter.
 * @param unused2     Unused parameter.
 * @return            0 on success, -1 on error.
 */
static int handle_prefix_build_notes(const HttpRequest *req, HttpResponse *res, const char *unused1,
                                     const char *unused2);

/**
 * @brief Serve the drive UI page for /drive prefix.
 *
 * @param req         Parsed HTTP request structure.
 * @param res         HTTP response structure to populate.
 * @param unused1     Unused parameter.
 * @param unused2     Unused parameter.
 * @return            0 on success, -1 on error.
 */
static int handle_prefix_drive(const HttpRequest *req, HttpResponse *res, const char *unused1,
                               const char *unused2);

/**
 * @brief Serve JSON files under the /expenses/ prefix.
 *
 * @param req         Parsed HTTP request structure.
 * @param res         HTTP response structure to populate.
 * @param unused1     Unused parameter.
 * @param unused2     Unused parameter.
 * @return            0 on success, -1 on error.
 */
static int handle_prefix_expenses_json(const HttpRequest *req, HttpResponse *res,
                                       const char *unused1, const char *unused2);

/**
 * @brief Guess the MIME type for a file based on its extension.
 *
 * @param path        Path to the file.
 * @return            MIME type string (e.g., "text/html").
 */
static const char *guess_mime_type(const char *path);

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */

/****************************************************************************
 * @section MIME type mapping
 * Table-driven mapping of file extensions to MIME types.
 * When you serve a file, you need to tell the browser what kind of file it is.
 ****************************************************************************/
static const struct
{
    const char *extension;
    const char *mime_type;
} mime_map[] = {
    {".html", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".json", "application/json"}, /*  manifest */
    {".md", "text/markdown"},      /* if ever want a real markdown mime */
    {".puml", "text/plain"}        /* PlantUML source */
};

/* Route matching type */
typedef enum
{
    ROUTE_EXACT, /* Exact match */
    ROUTE_PREFIX /* Prefix match (e.g. "/images/") */
} route_match_t;

/* Route handler signature */
typedef int (*route_handler_t)(const HttpRequest *, HttpResponse *, const char *, const char *);

/* Route table entry */
typedef struct
{
    const char *method;
    const char *path;
    route_match_t match_type;
    route_handler_t handler;
    const char *file_path;    /* For static files */
    const char *content_type; /* For static files */
} route_t;

/* Routing table */
static const route_t routes[] = {
    // --- Exact matches ---
    {HTTP_GET, URI_HOME, ROUTE_EXACT, handle_static, INDEX_PAGE, CONTENT_HTML},
    {HTTP_GET, URI_HOME_ALIAS, ROUTE_EXACT, handle_static, INDEX_PAGE, CONTENT_HTML},
    {HTTP_GET, URI_STYLE, ROUTE_EXACT, handle_static, STYLE_PAGE, CONTENT_CSS},
    {HTTP_GET, URI_WHOAMI, ROUTE_EXACT, handle_static, WHOAMI_PAGE, CONTENT_HTML},
    {HTTP_GET, URI_WHOAMI_API, ROUTE_EXACT, handle_whoami_api, NULL, NULL},
    {HTTP_GET, URI_DYNAMIC, ROUTE_EXACT, handle_static, DYNAMIC_PAGE, CONTENT_HTML},
    {HTTP_GET, URI_EXPENSES_PAGE, ROUTE_EXACT, handle_static, STATIC_ROOT "/expenses.html",
     CONTENT_HTML},
    {HTTP_GET, URI_EXPENSES_MONTHS, ROUTE_EXACT, handle_expenses_months, NULL, NULL},

    // --- Prefix matches ---
    {HTTP_GET, URI_IMAGES_PREFIX, ROUTE_PREFIX, handle_prefix_images, NULL, NULL},
    {HTTP_GET, URI_ASSETS_PREFIX, ROUTE_PREFIX, handle_prefix_assets, NULL, NULL},
    {HTTP_GET, URI_BUILD_NOTES_PREFIX, ROUTE_PREFIX, handle_prefix_build_notes, NULL, NULL},
    {HTTP_GET, URI_DRIVE_PREFIX, ROUTE_PREFIX, handle_prefix_drive, NULL, NULL},
    {HTTP_GET, URI_API_DRIVE_PREFIX, ROUTE_PREFIX, handle_drive_api, NULL, NULL},
    {HTTP_GET, URI_EXPENSES_PREFIX, ROUTE_PREFIX, handle_prefix_expenses_json, NULL, NULL},
};

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************/

int router_handle_request(const HttpRequest *request, HttpResponse *response)
{
    /* return variable */
    int res = STATUS_FAILURE;

    if(request == NULL || response == NULL)
    {
        log_error("router_handle_request: invalid arguments (request or response is NULL)", "");
        // send_404(response);
    }

    else if(strcmp(request->method, HTTP_GET) != 0)
    {
        // send_405(response);
    }

    else
    {
        for(size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i)
        {
            if(routes[i].match_type == ROUTE_EXACT)
            {
                if(strcmp(request->path, routes[i].path) == 0)
                {
                    res = STATUS_SUCCESS;
                    return routes[i].handler(request, response, routes[i].file_path,
                                             routes[i].content_type);
                }
            }
            else if(routes[i].match_type == ROUTE_PREFIX)
            {
                size_t len = strlen(routes[i].path);
                if(strncmp(request->path, routes[i].path, len) == 0)
                {
                    res = STATUS_SUCCESS;
                    return routes[i].handler(request, response, routes[i].file_path,
                                             routes[i].content_type);
                }
            }
        }
    }

    return res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************/

static int handle_static(const HttpRequest *req, HttpResponse *res, const char *file_path,
                         const char *content_type)
{
    (void)req;
    return handler_static_page(file_path, content_type, res);
}

static int handle_whoami_api(const HttpRequest *req, HttpResponse *res, const char *unused1,
                             const char *unused2)
{
    (void)unused1;
    (void)unused2;
    return handler_whoami(req, res);
}

static int handle_drive_api(const HttpRequest *req, HttpResponse *res, const char *unused1,
                            const char *unused2)
{
    (void)unused1;
    (void)unused2;
    return handler_drive(req, res);
}

static int handle_expenses_months(const HttpRequest *req, HttpResponse *res, const char *unused1,
                                  const char *unused2)
{
    (void)req;
    (void)unused1;
    (void)unused2;
    return handler_expenses(res);
}

static int handle_prefix_images(const HttpRequest *req, HttpResponse *res, const char *unused1,
                                const char *unused2)
{
    (void)unused1;
    (void)unused2;

    char file_path[HTTP_RECEIVE_BUFFER_LEN];
    snprintf(file_path, sizeof(file_path), "%s%s", STATIC_ROOT, req->path);
    if(access(file_path, R_OK) != 0)
    {
        // send_404(res);
        return 0;
    }
    return handler_static_page(file_path, guess_mime_type(file_path), res);
}

static int handle_prefix_assets(const HttpRequest *req, HttpResponse *res, const char *unused1,
                                const char *unused2)
{
    (void)unused1;
    (void)unused2;

    char file_path[HTTP_RECEIVE_BUFFER_LEN];
    snprintf(file_path, sizeof(file_path), "%s%s", STATIC_ROOT, req->path);
    if(strstr(file_path, ".."))
    {
        // send_404(res);
        return 0;
    }
    return handler_static_page(file_path, guess_mime_type(file_path), res);
}
static int handle_prefix_build_notes(const HttpRequest *req, HttpResponse *res, const char *unused1,
                                     const char *unused2)
{
    (void)unused1;
    (void)unused2;
    const char *rest = req->path + 12;
    if(*rest == '\0' || (rest[0] == '/' && rest[1] == '\0'))
    {
        return handler_static_page(BUILD_NOTES_PAGE, CONTENT_HTML, res);
    }
    if(rest[0] == '/')
    {
        char file_path[HTTP_RECEIVE_BUFFER_LEN];
        snprintf(file_path, sizeof file_path, "%s%s", STATIC_ROOT, req->path);
        if(strstr(req->path, "..") || access(file_path, R_OK) != 0)
        {
            // // send_404(res);
            return 0;
        }
        const char *mime = guess_mime_type(file_path);
        if(strstr(file_path, ".json")) mime = "application/json";
        return handler_static_page(file_path, mime, res);
    }
    // // send_404(res);
    return 0;
}
static int handle_prefix_drive(const HttpRequest *req, HttpResponse *res, const char *unused1,
                               const char *unused2)
{
    (void)req;
    (void)unused1;
    (void)unused2;
    return handler_static_page("www/drive.html", "text/html", res);
}

static int handle_prefix_expenses_json(const HttpRequest *req, HttpResponse *res,
                                       const char *unused1, const char *unused2)
{
    (void)unused1;
    (void)unused2;
    char fullpath[HTTP_RECEIVE_BUFFER_LEN];
    snprintf(fullpath, sizeof fullpath, STATIC_ROOT "%s", req->path);
    return handler_static_page(fullpath, "application/json", res);
}

static const char *guess_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if(!ext)
    {
        return "application/octet-stream";
    }
    for(size_t i = 0; i < sizeof(mime_map) / sizeof(mime_map[0]); ++i)
    {
        if(strcmp(ext, mime_map[i].extension) == 0)
        {
            return mime_map[i].mime_type;
        }
    }
    return "application/octet-stream";
}
