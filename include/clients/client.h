#ifndef SERVER_CLIENT_H
#define SERVER_CLIENT_H

#include <netdb.h>  // NI_MAXHOST, NI_MAXSERV
#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /****************************************************************************
     * PUBLIC STRUCTURED VARIABLES DECLARATIONS
     ****************************************************************************
     */
    /* None */

    /****************************************************************************
     * PUBLIC FUNCTIONS DECLARATIONS
     ****************************************************************************
     */

    void client_handle(const int client_fd);

#ifdef __cplusplus
}
#endif

#endif  // SERVER_CLIENT_H
