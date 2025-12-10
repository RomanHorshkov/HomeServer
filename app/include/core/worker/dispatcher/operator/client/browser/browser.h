#ifndef SERVER_BROWSER_H
#define SERVER_BROWSER_H

/****************************************************************************
 * PRIVATE INCLUDES
 ****************************************************************************
 */

#include "http_manager.h"

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief Handle a single client HTTP request: parse, dispatch, and respond.
 *
 * This is the main entry point for processing a client request. It performs:
 *   1. Parses the raw HTTP request buffer into a structured @ref Http_request_t.
 *   2. Dispatches the request to the router, which fills a @ref HttpResponse.
 *   3. Sends the HTTP response (headers and body) over the client socket.
 *   4. Frees any heap-allocated response body (e.g., from static file serving).
 *
 * All errors are logged. The function is binary-safe and supports both
 * keep-alive and close connection policies.
 *
 * @param fd                      Client socket descriptor.
 * @return Number of body bytes sent (may be 0 for no-body), or -1 on error.
 */
int browser_manage_client_req(int fd);

#endif /* SERVER_BROWSER_H */
