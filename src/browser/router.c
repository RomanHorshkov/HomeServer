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
#define URI_EXPENSES_PAGE "/expenses"
#define URI_EXPENSES_MONTHS "/api/expenses/months"
#define URI_IMAGES_PREFIX "/images/"

/** Content types */
#define CONTENT_HTML "text/html"
#define CONTENT_CSS "text/css"
#define BUILD_NOTES_DIR "www/build_notes/diagrams/"
#define BUILD_NOTES_PAGE "www/build_notes/index.html"

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
    {".html", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".json", "application/json"},  // for manifest
    {".md", "text/markdown"},       // if ever want a real markdown mime
    {".puml", "text/plain"}         // PlantUML source
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

static int has_suffix(const char *str, const char *suffix);

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

    /* ------------------------------------------------------------------
     * Serve Static assets (under /images)
     * ------------------------------------------------------------------ */
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

    /* ------------------------------------------------------------------
     * Serve Drive page
     * ------------------------------------------------------------------ */
    /* Drive UI page */
    if(strncmp(request->path, "/drive", 6) == 0)
    {
        return static_page_serve_file("www/drive.html", "text/html", response);
    }

    /* Drive UI page: JSON API: /api/drive?path=/some/subdir */
    if(strncmp(request->path, "/api/drive", 10) == 0)
    {
        return drive_json_handler(request, response);
    }

    /* ------------------------------------------------------------------
     * Serve build_notes page
     * ------------------------------------------------------------------ */
    if(strncmp(request->path, "/build_notes", 12) == 0)
    {
        const char *rest = request->path + 12;  // points at either '\0' or "/…"
        if(*rest == '\0' || (rest[0] == '/' && rest[1] == '\0'))
        {
            // exact /build_notes or /build_notes/
            return static_page_serve_file(BUILD_NOTES_PAGE, CONTENT_HTML, response);
        }
        if(rest[0] == '/')
        {
            // anything under /build_notes/…
            char file_path[PATH_MAX];
            snprintf(file_path, sizeof file_path, "%s%s", STATIC_ROOT, request->path);
            if(strstr(request->path, "..") || access(file_path, R_OK) != 0)
            {
                send_404(response);
                return 0;
            }
            const char *mime = guess_mime_type(file_path);
            if(strstr(file_path, ".json")) mime = "application/json";
            return static_page_serve_file(file_path, mime, response);
        }
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

    /* ─────────────────────────────────────────────────────────────
     * Expenses feature
     * ───────────────────────────────────────────────────────────── */
    /* “list months” API */
    if(strcmp(request->path, "/api/expenses/months") == 0)
    {
        return expenses_months_handler(response);
    }

    /* Serve raw JSON files under /expenses/YYYY/MM.json */
    if(strncmp(request->path, "/expenses/", 9) == 0 && has_suffix(request->path, ".json"))
    {
        /* build filesystem path: STATIC_ROOT + request->path */
        char fullpath[PATH_MAX];
        snprintf(fullpath, sizeof fullpath, STATIC_ROOT "%s", request->path);
        return static_page_serve_file(fullpath, "application/json", response);
    }

    /* Serve the HTML page as before: */
    if(strcmp(request->path, URI_EXPENSES_PAGE) == 0)
    {
        return static_page_serve_file(STATIC_ROOT "/expenses.html", CONTENT_HTML, response);
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

/**
 * @brief  Check whether `str` ends with `suffix`.
 * @param  str     The full string to test.
 * @param  suffix  The ending substring to look for.
 * @return         1 if `str` ends with `suffix`, 0 otherwise.
 */
static int has_suffix(const char *str, const char *suffix)
{
    if(!str || !suffix)
    {
        return 0;
    }

    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);

    /* suffix longer than string? cannot match */
    if(lensuffix > lenstr)
    {
        return 0;
    }

    /* compare tail of str with suffix */
    return strcmp(str + (lenstr - lensuffix), suffix) == 0;
}