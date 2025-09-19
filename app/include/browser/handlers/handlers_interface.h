/**
 * @file handlers.h
 * @brief Central include for all HTTP API endpoint handlers.
 *
 * This header aggregates all dynamic API endpoint handler declarations
 * for the browser-facing layer of the server. Each handler implements
 * a specific REST endpoint, receives a parsed immutable `HttpRequest`,
 * and populates a fresh `HttpResponse` struct. No handler performs
 * socket I/O; network transmission and memory cleanup are managed by
 * the caller (router/browser).
 *
 * ### Implemented endpoints
 * - `handler_whoami()`      — GET `/api/whoami`         (server info & request echo)
 * - `handler_drive()`       — GET `/api/drive?path=/…`  (directory listing)
 * - `handler_expenses()`    — GET `/api/expenses/months` (list months with expenses)
 * - *(future)* `expenses_add_handler()` — POST `/api/expenses` (add expense)
 *
 * ### Threading model
 * All handlers are re-entrant and stateless, safe for concurrent use.
 *
 * ### Error handling
 * Handlers never write to sockets. They must always fill:
 *   - `status_code`    (e.g. 200, 400, 404)
 *   - `content_type`   (e.g. "application/json")
 *   - `body`           (heap-allocated or static; ownership transferred)
 *   - `body_length`    (exact byte length)
 * Return 0 on success, -1 on fatal error (e.g. OOM).
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025-05-11
 */
#ifndef SERVER_BROWSER_HANDLER_INTERFACE_H
#define SERVER_BROWSER_HANDLER_INTERFACE_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */

// #include "handler_drive.h"
// #include "handler_expenses.h"
// #include "handler_static.h"
#include "server_settings.h"

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief Serve a static file in response to an HTTP request.
 *
 * This handler constructs a file path from the request path, attempts to open and read
 * the file from disk, and fills the provided HttpResponse structure with the file's
 * contents and appropriate headers. The MIME type is automatically determined based
 * on the file extension.
 *
 * On error (file not found, read error, etc.), logs the error and returns non-zero.
 * The caller is responsible for freeing the response body buffer if the call succeeds.
 *
 * @param request  Pointer to the parsed HttpRequest structure.
 * @param response Pointer to the HttpResponse structure to populate.
 * @retval  0      Success; response is fully populated.
 * @retval !=0     Failure; response is not valid.
 */
int handler_static(const HttpRequest* request, HttpResponse* response);

/**
 * @brief Build an “echo” JSON response for <tt>/api/whoami</tt>.
 *
 * The handler is primarily a debugging/diagnostic endpoint.
 * It returns the server’s current UTC timestamp (to millisecond precision)
 * plus a verbatim echo of the incoming request.
 *
 *
 * @param[in]  req  Parsed HTTP request (method, path, headers).
 * @param[out] res  Response structure to fill.
 *
 * @retval 0  Success.
 * @retval −1 Unexpected allocation/encoding failure (very unlikely).
 *
 * @note The function never allocates more than a few kilobytes of memory,
 *       so there is no hard limit on the header count beyond the one already
 *       imposed by @c HttpRequest.
 */
int handler_whoami(const HttpRequest* req, HttpResponse* res);

/**
 * @brief Enumerate every month for which an expense JSON file exists.
 *
 * Handles the **GET /api/expenses/months** endpoint by scanning the directory
 * structure:
 *   www/expenses/YYYY/MM.json
 * and collecting all (YYYY, MM) pairs that match the pattern.
 *
 * The response is a sorted JSON array of strings, e.g.:
 *   ["2023-11", "2024-02", "2025-01"]
 *
 * No request parameters are required.
 *
 * Ownership:
 *   The response body buffer is allocated using cJSON_PrintUnformatted().
 *   The caller is responsible for freeing it with free().
 *
 * @param[in]  req   Pointer to the parsed HttpRequest (unused).
 * @param[out] resp  Pointer to the HttpResponse to populate.
 *
 * @retval  0  Success – resp is populated (HTTP 200).
 * @retval -1  Fatal error (e.g., memory allocation failure).
 */
int handler_expenses(const HttpRequest* req, HttpResponse* resp);

/**
 * @brief Return a JSON directory listing for the “Drive” feature.
 *
 * The endpoint handles requests of the form
 * `GET /api/drive?path=/some/sub/dir`.
 *
 * * The query‑string key is literally `path`.
 * * The *virtual* root of the drive is the project folder <tt>www/</tt>.
 *   A request for `?path=/foo` will therefore inspect `www/foo/` on disk.
 *
 * Security / validation
 * ---------------------
 * The handler rejects:
 * - Query paths that resolve outside the <tt>www/</tt> tree (`".."` check).
 * - Paths whose decoded length exceeds <tt>PATH_MAX</tt>.
 *
 * Response on success (`HTTP 200`)
 * ```json
 * {
 *   "path": "/requested/path",
 *   "items": [
 *     { "name": "file.txt", "type": "file" },
 *     { "name": "subdir",   "type": "directory" }
 *   ]
 * }
 * ```
 * Response on error (`HTTP 400`, `404`, …) is a JSON object with an `"error"` key.
 *
 * Memory ownership
 * ----------------
 * On success the function allocates the JSON body with
 * `cJSON_PrintUnformatted()`.  Ownership is transferred to the caller
 * (server core) which must eventually `free()` it.
 *
 * @param[in]  req  Parsed HTTP request structure.
 * @param[out] resp Response structure to populate.
 *
 * @retval 0  Success – @p resp is filled, status 200 or 404.
 * @retval -1 INTERNAL ERROR (allocation failure, etc.).
 */
int handler_drive(const HttpRequest* req, HttpResponse* resp);

#endif /* SERVER_BROWSER_HANDLER_INTERFACE_H */
