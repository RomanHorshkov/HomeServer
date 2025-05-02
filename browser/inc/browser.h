#ifndef SERVER_BROWSER_H
#define SERVER_BROWSER_H


/****************************************************************************
 * PRIVATE INCLUDES
 ****************************************************************************
 */

#include "http_manager.h"
#include <unistd.h>                 // ssize_t


/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

size_t browser_manage_client_req(char* recv_buf, size_t n, char* send_buf, int* client_connection_policy);


#endif /* SERVER_BROWSER_H */