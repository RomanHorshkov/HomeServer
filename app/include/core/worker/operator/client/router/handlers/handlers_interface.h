/**
 * @file handlers.h
 * @brief Central include for all HTTP API endpoint handlers.
 *
 * This header aggregates all dynamic API endpoint handler declarations
 * for the browser-facing layer of the server. Each handler implements
 * a specific REST endpoint, receives a parsed immutable `Http_request_t`,
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

#include "http_common.h"
#include "config_core.h"

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

int handler_static(const http_request_t* request, http_response_t* response);

int handler_whoami(const http_request_t* req, http_response_t* res);

int handler_database(const http_request_t* req, http_response_t* res);

#endif /* SERVER_BROWSER_HANDLER_INTERFACE_H */
