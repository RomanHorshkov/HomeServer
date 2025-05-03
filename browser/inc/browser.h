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

ssize_t browser_manage_client_req(int fd, const char* recv_buf, size_t n,
                                  int* client_connection_policy);

#endif /* SERVER_BROWSER_H */