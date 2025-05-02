#ifndef SERVER_BROWSER_H
#define SERVER_BROWSER_H


/****************************************************************************
 * PRIVATE INCLUDES
 ****************************************************************************
 */

#include <unistd.h>                 // ssize_t


/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

int browser_manage_client_req(char* recv_buf, size_t n, char* send_buf);


#endif /* SERVER_BROWSER_H */