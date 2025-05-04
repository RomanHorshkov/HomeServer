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
#include "static_page.h"

/****************************************************************************
 * @section Defines and Constants
 ****************************************************************************/

/** Root directory for all static assets */
#define PATH_MAX 4096 /* # chars in a path name including nul */
#define STATIC_ROOT "www"

/** Default file paths */
#define INDEX_PAGE STATIC_ROOT "/index.html"
#define WHOAMI_PAGE STATIC_ROOT "/whoami.html"
#define DYNAMIC_PAGE STATIC_ROOT "/dynamic.html"
#define STYLE_PAGE STATIC_ROOT "/style.css"

/** URI prefixes and routes */
#define URI_HOME "/"
#define URI_HOME_ALIAS "/home"
#define URI_STYLE "/style.css"
#define URI_WHOAMI "/whoami"
#define URI_WHOAMI_API "/api/whoami"
#define URI_DYNAMIC "/dynamic"
#define URI_IMAGES_PREFIX "/images/"

/** Content types */
#define CONTENT_HTML "text/html"
#define CONTENT_CSS "text/css"

/** HTTP methods */
#define HTTP_GET "GET"

/****************************************************************************
 * @section MIME type mapping
 * Table-driven mapping of file extensions to MIME types.
 ****************************************************************************/
static const struct
{
    const char *extension;
    const char *mime_type;
} mime_map[] = {
    {".html", "text/html"}, {".css", "text/css"},      {".js", "application/javascript"},
    {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},   {".png", "image/png"},
    {".gif", "image/gif"},  {".svg", "image/svg+xml"},
};

/****************************************************************************
 * @section Private Function Prototypes
 ****************************************************************************/

/** Guess MIME type by file extension */
static const char *guess_mime_type(const char *path);

/** Send a 404 Not Found response */
static void send_404(HttpResponse *response);

/** Send a 405 Method Not Allowed response */
static void send_405(HttpResponse *response);

/****************************************************************************
 * @section Public Functions
 ****************************************************************************/

int router_handle_request(const HttpRequest *request, HttpResponse *response)
{
    if(!request || !response)
    {
        return -1;
    }

    /* Only GET is supported */
    if(strcmp(request->method, HTTP_GET) != 0)
    {
        send_405(response);
        return 0;
    }

    /* Route GET requests */
    if(strcmp(request->path, URI_STYLE) == 0)
    {
        return static_page_serve_file(STYLE_PAGE, CONTENT_CSS, response);
    }
    if(strcmp(request->path, URI_HOME) == 0 || strcmp(request->path, URI_HOME_ALIAS) == 0)
    {
        return static_page_serve_file(INDEX_PAGE, CONTENT_HTML, response);
    }
    if(strcmp(request->path, URI_WHOAMI) == 0)
    {
        return static_page_serve_file(WHOAMI_PAGE, CONTENT_HTML, response);
    }
    if(strcmp(request->path, URI_WHOAMI_API) == 0)
    {
        return whoami_json_handler(request, response);
    }
    if(strcmp(request->path, URI_DYNAMIC) == 0)
    {
        return static_page_serve_file(DYNAMIC_PAGE, CONTENT_HTML, response);
    }

    /* Serve static assets under /images/ */
    if(strncmp(request->path, URI_IMAGES_PREFIX, sizeof(URI_IMAGES_PREFIX) - 1) == 0)
    {
        char file_path[PATH_MAX];
        snprintf(file_path, sizeof(file_path), "%s%s", STATIC_ROOT, request->path);

        log_info("ROUTER: Trying to serve image from disk: %s", file_path);
        /* TODO: Sanitize file_path to prevent path traversal */
        if(access(file_path, R_OK) != 0)
        {
            log_info("ROUTER: Image not found or unreadable: %s", file_path);
            send_404(response);
            return 0;
        }
        log_info("ROUTER: static_page_serve_file returned %s", file_path);
        return static_page_serve_file(file_path, guess_mime_type(file_path), response);
    }

    /* Drive UI page */
    if(strcmp(request->path, "/drive") == 0)
    {
        return static_page_serve_file("www/drive.html", "text/html", response);
    }

    /* Drive JSON API: /api/drive?path=/some/subdir */
    else if(strncmp(request->path, "/api/drive", 10) == 0)
    {
        return drive_json_handler(request, response);
    }

    /* ------------------------------------------------------------------
     * Serve any file under /assets/ from disk
     * ------------------------------------------------------------------ */
    if(strncmp(request->path, "/assets/", 8) == 0)
    {
        char file_path[PATH_MAX];
        snprintf(file_path, sizeof file_path, "%s%s", STATIC_ROOT, request->path);

        /* Guard against “..” path‑traversal attempts */
        if(strstr(file_path, ".."))
        {
            send_404(response);
            return 0;
        }

        return static_page_serve_file(file_path, guess_mime_type(file_path), response);
    }

    /* Fallback to 404 Not Found */
    send_404(response);
    return 0;
}

/****************************************************************************
 * @section Private Functions Definitions
 ****************************************************************************/

static void send_404(HttpResponse *response)
{
    response->status_code = 404;
    response->status_text = "Not Found";
    response->content_type = CONTENT_HTML;
    response->body = "<html><body><h1>404 Not Found</h1></body></html>";
    response->body_length = strlen(response->body);
}

static void send_405(HttpResponse *response)
{
    response->status_code = 405;
    response->status_text = "Method Not Allowed";
    response->content_type = CONTENT_HTML;
    response->body = "<html><body><h1>405 Method Not Allowed</h1></body></html>";
    response->body_length = strlen(response->body);
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
