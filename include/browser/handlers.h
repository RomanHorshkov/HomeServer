/**
 * @file handlers.h
 * @brief **High‑level HTTP endpoint dispatchers**
 *
 * This header exposes one function per dynamic **API endpoint** served by the
 * web‑browser layer of the application.  Every handler receives a parsed
 * immutable request (`HttpRequest`) and populates a fresh response structure
 * (`HttpResponse`).  No handler performs any socket I/O – the caller
 * (router/browser) owns network transmission and memory‑cleanup.
 *
 * ### Currently implemented endpoints
 *
 * | Function                              | HTTP verb | Public URL                         |
 * Purpose                                   |
 * |--------------------------------------
 * |-----------|------------------------------------|-------------------------------------------| |
 * `whoami_json_handler()`               | `GET`     | `/api/whoami`                      | Echo
 * request & server timestamp           | | `drive_json_handler()`                | `GET`     |
 * `/api/drive?path=/…`               | Directory listing inside *www/drive*      | |
 * `expenses_months_handler()`           | `GET`     | `/api/expenses/months`             |
 * Enumerate months which contain expenses   | | *(future)* `expenses_add_handler()`   | `POST`    |
 * `/api/expenses` (TBD)              | Append a new expense record               |
 *
 * ### Threading / forking model
 * Handlers are designed to be **re‑entrant** and contain no static global
 * state.  They may therefore be executed concurrently from multiple child
 * processes created by the `fork()`‑based server core.
 *
 * ### Error handling contract
 * A handler **never** writes to the socket.
 * It must always fill at least these `HttpResponse` members:
 *
 * * `status_code`   — HTTP status (e.g. `200`, `400`, `404`)
 * * `content_type` — MIME type string (e.g. `"application/json"`)
 * * `body`         — Dynamically allocated buffer *or* pointer to a
 *   static literal; ownership is transferred to the caller.
 * * `body_length`  — Exact byte length of `body`.
 *
 * The handler returns **`0` on success** and **`‑1` on fatal error**
 * (typically memory exhaustion).  In the latter case the caller should send
 * an internal‐error response.
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑05‑11
 */

#ifndef SERVER_BROWSER_HANDLER_H
#define SERVER_BROWSER_HANDLER_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */
#include <errno.h>   // errno, EADDRINUSE, etc.
#include <string.h>  // memset(), strcpy(), strlen(), strerror(), etc.

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
 */

#include "router.h"

/**
 * @brief Build an “echo” JSON response for <tt>/api/whoami</tt>.
 *
 * The handler is primarily a debugging/diagnostic endpoint.
 * It returns the server’s current UTC timestamp (to millisecond precision)
 * plus a verbatim echo of the incoming request:
 *
 * ```
 * HTTP/1.1 200 OK
 * Content‑Type: application/json
 *
 * {
 *   "server_time": "2025‑05‑11T19:23:45.123Z",
 *   "method"     : "GET",
 *   "path"       : "/api/whoami",
 *   "headers"    : {
 *     "Host"          : "localhost:3490",
 *     "User‑Agent"    : "curl/8.0.1",
 *     ...
 *   }
 * }
 * ```
 *
 * Ownership model
 * ---------------
 * The function allocates the JSON body with `cJSON_PrintUnformatted()`;
 * the pointer is stored in `res->body` and **ownership is transferred**
 * to the caller (usually the browser layer), which must eventually free it
 * with `free()`.
 *
 * Thread/process safety
 * ---------------------
 * The handler is *re‑entrant* and side‑effect‑free; it touches only
 * stack variables and the `HttpRequest` / `HttpResponse` structures passed
 * by the caller.
 *
 * @param[in]  req  Parsed HTTP request (method, path, headers).
 * @param[out] res  Response structure to fill.
 *                  On success all fields are populated (status 200, JSON body).
 *
 * @retval 0  Success.
 * @retval −1 Unexpected allocation/encoding failure (very unlikely).
 *
 * @note The function never allocates more than a few kilobytes of memory,
 *       so there is no hard limit on the header count beyond the one already
 *       imposed by @c HttpRequest.
 */
int whoami_json_handler(const HttpRequest* req, HttpResponse* res);

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
int drive_json_handler(const HttpRequest* req, HttpResponse* resp);

/**
 * @brief Enumerate every month for which an expense JSON file exists.
 *
 * The endpoint services **GET /api/expenses/months** and scans the directory
 * tree
 * ~~~text
 *   www/expenses/YYYY/MM.json
 * ~~~
 * collecting all pairs ( `YYYY`, `MM` ) that match the pattern.
 * The response is a sorted JSON array, e.g.
 * ~~~json
 * ["2023‑11","2024‑02","2025‑01"]
 * ~~~
 *
 * *No request parameters are required.*
 *
 * Ownership rules
 * ---------------
 * The function allocates the body buffer with `cJSON_PrintUnformatted()`;
 * the caller (server core) becomes responsible for `free()`‑ing it.
 *
 * @param[out] resp  Pre‑allocated response object to fill.
 *
 * @retval 0  Success – @p resp is populated (always HTTP 200).
 * @retval -1 Fatal error (memory allocation failure).
 */
int expenses_months_handler(HttpResponse* resp);

#endif /* SERVER_BROWSER_HANDLER_H */