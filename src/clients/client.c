#define _GNU_SOURCE

/****************************************************************************
 * PRIVATE INCLUDES
 ****************************************************************************
 */
#include "client.h"

#include <errno.h>       // errno, EADDRINUSE, etc.
#include <netdb.h>       // socklen_t, NI_MAXHOST, NI_MAXSERV
#include <stdio.h>       // snprintf(), etc
#include <stdlib.h>      // malloc(), calloc() etc
#include <string.h>      // memset(), strcpy(), strlen(), etc.
#include <sys/socket.h>  // socklen_t, socket(), bind(), setsockopt(), etc.
#include <sys/time.h>    // struct timeval

#include "browser.h"
#include "logger.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

static int set_client_socket_initial_options(int socket_fd);

static int upgrade_client_socket_options(int socket_fd);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

void client_handle(const int client_fd)
{
    /* set a connections counter */
    int received_first_time = 0;

    /* Set beginning socket options */
    set_client_socket_initial_options(client_fd);

    /* receive buffer */
    char recv_buff[HTTP_RECEIVE_BUFFER_LEN];
    /* receive buffer len */
    ssize_t n;

    /* Client's connection policy over http */
    int client_connection_policy = -1;

    /* recv is BLOCKING here */
    while((n = recv(client_fd, recv_buff, sizeof(recv_buff), 0)) > 0)
    {
        log_info("Client [pid %d fd %d]: received, parsing ->", getpid(), client_fd);

        /* serve the request; if any and respond */
        if(browser_manage_client_req(client_fd, recv_buff, (size_t)n, &client_connection_policy) <=
           0)
        {
            log_error("Client: browser failed to manage request", strerror(errno));
        }

        /* Check if the client wants the connection to close */
        else if(client_connection_policy == HTTP_CONNECTION_CLOSE)
        {
            /* exit and close the client */
            log_info("Client: [pid %d fd %d]: closes connection", getpid(), client_fd);
            break;
        }

        /* Upgrade after first msg received the client's socket options */
        else if(!received_first_time && upgrade_client_socket_options(client_fd) == 0)
        {
            received_first_time = 1;
            log_info("Client: upgraded timeout for fd=%d to %ds\n", client_fd,
                     CLIENT_MAX_TIMEOUT_S_L);
        }
    }

    if(n == 0)
    {
        log_info("Client: fd=%d closed connection.\n", client_fd);
    }
    else if(errno != EAGAIN && errno != EWOULDBLOCK)
    {
        log_error("Client: recv: %s", strerror(errno));
    }

    /* Close client */
    close(client_fd);

    log_info("Client handeled; Connection closed.");
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int set_client_socket_initial_options(int socket_fd)
{
    /* return value */
    int res = -1;

    /* Timeout */
    struct timeval timeout = {CLIENT_MAX_TIMEOUT_S, CLIENT_MAX_TIMEOUT_MS};

    if(setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        /* log error if set sock opt fails and return -1 */
        log_error("Clients: set_client_socket_initial_options timeout: %s\n", strerror(errno));
    }
    else
    {
        res = 0;
    }

    return res;
}

static int upgrade_client_socket_options(int socket_fd)
{
    /* return value */
    int res = -1;

    /* Timeout */
    struct timeval timeout = {CLIENT_MAX_TIMEOUT_S_L, CLIENT_MAX_TIMEOUT_MS};

    if(setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        log_error("Clients: upgrade_client_socket_options timeout: %s\n", strerror(errno));
    }
    else
    {
        res = 0;
    }
    return res;
}
