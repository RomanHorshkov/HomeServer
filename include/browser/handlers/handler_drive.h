#ifndef SERVER_BROWSER_HANDLER_DRIVE_H
#define SERVER_BROWSER_HANDLER_DRIVE_H

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

/**
 * PUBLIC FUNCTIONS DECLARATIONS
 */

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

#endif /* SERVER_BROWSER_HANDLER_DRIVE_H */
