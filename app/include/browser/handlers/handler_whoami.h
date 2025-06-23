#ifndef SERVER_BROWSER_HANDLER_WHOAMI_H
#define SERVER_BROWSER_HANDLER_WHOAMI_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */

#include "http_manager.h" /* HttpRequest, HttpResponse */

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
int handler_whoami(const HttpRequest* req, HttpResponse* res);

#endif /* SERVER_BROWSER_HANDLER_WHOAMI_H */
