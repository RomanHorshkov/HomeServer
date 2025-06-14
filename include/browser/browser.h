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

/*
 * Public: browser_manage_client_req
 * ---------------------------------
 * Parses an incoming HTTP request, routes it, and sends the response.
 * Frees any heap-allocated body after sending.
 * Parameters:
 *   fd - Client socket descriptor
 *   recv_buf - Buffer containing raw HTTP request data
 *   n - Number of bytes in recv_buf
 *   client_connection_policy - OUT param set to CONNECTION_CLOSE or KEEP_ALIVE
 * Returns:
 *   Number of bytes of body sent on success, -1 on error.
 */
ssize_t browser_manage_client_req(int fd, const char* recv_buf, size_t n,
                                  int client_connection_policy);

#endif /* SERVER_BROWSER_H */
