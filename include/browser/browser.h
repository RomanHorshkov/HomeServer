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
 *   1. Parses the raw HTTP request buffer into a structured @ref HttpRequest.
 *   2. Dispatches the request to the router, which fills a @ref HttpResponse.
 *   3. Sends the HTTP response (headers and body) over the client socket.
 *   4. Frees any heap-allocated response body (e.g., from static file serving).
 *
 * All errors are logged. The function is binary-safe and supports both
 * keep-alive and close connection policies.
 *
 * @param fd                      Client socket descriptor.
 * @param recv_buf                Pointer to the received HTTP request buffer.
 * @param n                       Number of bytes in @p recv_buf.
 * @param client_connection_policy Connection policy (in/out): HTTP_CONNECTION_CLOSE or KEEP_ALIVE.
 * @return Number of body bytes sent (may be 0 for no-body), or -1 on error.
 */
ssize_t browser_manage_client_req(int fd, const char* recv_buf, size_t n,
                                  int client_connection_policy);

#endif /* SERVER_BROWSER_H */
