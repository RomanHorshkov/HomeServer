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
#include "server_settings.h"


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

static int set_socket_initial_options(int socket_fd);

static int upgrade_client_socket_options(int socket_fd);



/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int clients_init(client_t *clients_ptr)
{
    /* return variable */
    int ret = STATUS_FAILURE;

    /* Allocate array of clients */
    client_t *new_clients = calloc(MAX_CLIENTS, sizeof(client_t));

    /* Allocate memory for all sockets for each clients */
    socket_info_t *new_sockets = calloc(MAX_SOCKETS_PER_CLIENT * MAX_CLIENTS, sizeof(socket_info_t));

    if (new_clients != NULL && new_sockets != NULL)
    {
        /* set the sockets of the future clients */
        for (size_t i = 0; i < MAX_CLIENTS; ++i)
        {
            new_clients[i].sockets = new_sockets + (i * MAX_SOCKETS_PER_CLIENT);
            // new_clients[i].sockets_count = 0; // calloc sets to 0
            new_clients[i].sockets_capacity = MAX_SOCKETS_PER_CLIENT;
        }
        /* set the clients to the caller */
        clients_ptr = new_clients;

        /* Set return variable to success */
        ret = STATUS_SUCCESS;

#ifdef DEBUG_MODE_MODE
        log_info("Clients: created NULL");
#endif
    }

    {
        log_error("Clients: failed to allocate memory: %s", strerror(errno));
        free(new_clients);
        free(new_sockets);
    }

    return ret;
}

int client_add_socket(client_t *client, const int file_descriptor, const char *host_name, const char *port_name)
{
    /* return value */
    int ret = STATUS_FAILURE;

    if(client == NULL || file_descriptor < 0 || host_name == NULL || port_name == NULL)
    {
        log_error("Client: client_add_socket: invalid input parameters");
    }
    else if(client->sockets_count >= MAX_SOCKETS_PER_CLIENT)
    {
        log_error("Client: client_add_socket: trying to add more sockets than capacity [%zu]",
                  MAX_SOCKETS_PER_CLIENT);
    }
    else
    {
        /* Set the socket info */
        client->sockets[client->sockets_count].fd = file_descriptor;
        strncpy(client->sockets[client->sockets_count].host, host_name, NI_MAXHOST);
        strncpy(client->sockets[client->sockets_count].port, port_name, NI_MAXSERV);

        /* Increase the sockets count */
        client->sockets_count++;

        /* Set return value to success */
        ret = STATUS_SUCCESS;
    }

    return ret;
}

int client_add_client(client_t *client, const int file_descriptor, const char *host_name, const char *port_name)
{
    /* return value */
    int ret = STATUS_FAILURE;

    if(file_descriptor < 0 || host_name == NULL || port_name == NULL)
    {
        log_error("Client: client_add_client: invalid input parameters");
    }
    else if(client->sockets_count >= MAX_SOCKETS_PER_CLIENT)
    {
        log_error("Client: client_add_client: trying to add more sockets than capacity [%zu]",
                  MAX_SOCKETS_PER_CLIENT);
    }
    else
    {
        /* Set the socket info */
        client->sockets[client->sockets_count].fd = file_descriptor;
        strncpy(client->sockets[client->sockets_count].host, host_name, NI_MAXHOST);
        strncpy(client->sockets[client->sockets_count].port, port_name, NI_MAXSERV);

        /* Increase the sockets count */
        client->sockets_count++;

        /* Set return value to success */
        ret = STATUS_SUCCESS;
    }

    return ret;
}

void client_handle(const int client_fd)
{
    /* set a connections counter */
    int received_first_time = 0;

    /* Set beginning socket options */
    set_socket_initial_options(client_fd);

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

int client_get_host(const client_t *client, char* host_name, const size_t host_name_len)
{
    /* return value */
    int res = STATUS_FAILURE;

    if(client == NULL || host_name == NULL || host_name_len == 0)
    {
        log_error("Client: client_get_host: invalid input parameters");
    }
    else
    {
        /* Copy the host name to the provided buffer */
        res = snprintf(host_name, host_name_len, "%s", client->host);

        /* Check if snprintf was successful */
        if(res < 0 || (size_t)res >= host_name_len)
        {
            log_error("Client: client_get_host: snprintf failed or buffer too small");
        }
        else
        {
            res = STATUS_SUCCESS; // Successfully copied the host name
        }

    }

    return res;
}


/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int set_socket_initial_options(int socket_fd)
{
    /* return value */
    int res = -1;

    /* Timeout */
    struct timeval timeout = {CLIENT_MAX_TIMEOUT_S, CLIENT_MAX_TIMEOUT_MS};

    if(setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        /* log error if set sock opt fails and return -1 */
        log_error("Clients: set_socket_initial_options timeout: %s\n", strerror(errno));
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
