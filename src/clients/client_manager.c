#define _GNU_SOURCE

/****************************************************************************
 * PRIVATE INCLUDES
 ****************************************************************************
 */

#include "client_manager.h"

#include <errno.h>       // errno, EADDRINUSE, etc.
#include <netdb.h>       // socklen_t, NI_MAXHOST, NI_MAXSERV
#include <stdio.h>       // snprintf(), etc
#include <stdlib.h>      // malloc(), calloc() etc
#include <string.h>      // memset(), strcpy(), strlen(), etc.
#include <sys/socket.h>  // socklen_t, socket(), bind(), setsockopt(), etc.
#include <unistd.h>      // close()

#include "client.h"
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

struct client_manager
{
    struct client *clients;         // dynamic array of clients (processes)
    size_t clients_count;           // active clients count
    size_t clients_capacity;        // clients list size
};

/****************************************************************************
 * PRIVATE FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief
 * @return
 */
static int add_new_client(client_manager_t *mgr, const struct sockaddr *socket_address,
                               socklen_t socket_address_len, int file_descriptor);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int client_manager_init(client_manager_t *client_manager)
{
    /* return variable */
    int ret = STATUS_FAILURE;

    /* allocate space for client_manager
    calloc sets to 0 the members */
    client_manager_t *new_client_manager = calloc(MAX_CLIENT_MANAGERS, sizeof(client_manager_t));

    /* check if allocation successful */
    if(new_client_manager == NULL)
    {
        log_error("ClientManager: failed to allocate memory: %s", strerror(errno));
    }

    /* Initialize clients */
    else if(clients_init(new_client_manager->clients) == STATUS_FAILURE)
    {
        log_error("ClientManager: failed to initialize clients: %s", strerror(errno));
        free(new_client_manager);
    }

    else
    {
        /* set return variable to success */
        ret = STATUS_SUCCESS;

        /* set the client manager to the caller */
        client_manager = new_client_manager;

        log_info("ClientManager: created NULL");
    }

    return ret;
}


int client_manager_add_client(client_manager_t *mgr, const struct sockaddr_storage *addr,
                              socklen_t addrlen, int *file_descriptor)
{
    /* return variable */
    int ret = STATUS_FAILURE;

    /* check input */
    if(mgr == NULL || addr == NULL || addrlen == 0 || *file_descriptor <= 0)
    {
        errno = EINVAL;
        log_error("ClientManager: client_manager_add_client(): invalid input", strerror(errno));
    }
    
    /* Check if any client already exists */
    else if (mgr->clients == NULL || mgr->clients_count == 0)
    {
        #ifdef DEBUG_MODE
        log_info("ClientManager: no existing clients, creating new process");
        #endif
        ret = CLIENT_MANAGER_CLIENT_NOT_EXISTS;
    }
    
    /* Check if socket belongs to existent client or a new client is needed */
    else
    {
        /* set temporary buffers for host string, ip and port */
        char client_ipstr[NI_MAXHOST] = {0};
        char ipstr[NI_MAXHOST] = {0};
        char portstr[NI_MAXSERV] = {0};

        /* get info about connecting client */
        int gntret = getnameinfo((struct sockaddr *)addr, addrlen,
                                ipstr, NI_MAXHOST,
                                portstr, NI_MAXSERV,
                                NI_NUMERICHOST | NI_NUMERICSERV);
        /* check if getnameinfo succeeded */
        if (gntret != 0)
        {
            log_error("ClientManager: getnameinfo failed: %s", gai_strerror(gntret));
            ret = STATUS_FAILURE;
        }
        else
        {
            /* Search for existing client with this IP */
            for (size_t i = 0; i < mgr->clients_count; i++)
            {
                client_get_host(&(mgr->clients[i]), client_ipstr, NI_MAXHOST);
                if (strcmp(ipstr, client_ipstr) == 0)
                {
                    /* Found existing client for this IP */
                    #ifdef DEBUG_MODE
                    log_info("ClientManager: found existing client for IP %s", ipstr);
                    #endif

                    /* Add the socket to the client's sockets pool */
                    if (client_add_socket(&(mgr->clients[i]), *file_descriptor, ipstr, portstr) != STATUS_SUCCESS)
                    {
                        log_error("ClientManager: failed to add socket to existing client: %s", strerror(errno));
                        ret = STATUS_FAILURE;
                    }
                    else
                    {
                        ret = CLIENT_MANAGER_CLIENT_EXISTS;
                    }

                    break;
                }
                else
                {
                    /* New client for this IP */
                    ret = CLIENT_MANAGER_CLIENT_NOT_EXISTS;
                }
            }

            if (ret == CLIENT_MANAGER_CLIENT_NOT_EXISTS)
            {
                /* Add the client */
                if(client_add_client(&(mgr->clients[mgr->clients_count]), *file_descriptor, ipstr, portstr) != STATUS_SUCCESS)
                {
                    log_error("ClientManager: failed to add new client: %s", strerror(errno));
                    ret = STATUS_FAILURE;
                }
                else
                {
                    ret = CLIENT_NEW_CLIENT_CREATED;
                    #ifdef DEBUG_MODE
                    log_info("ClientManager: added new client with fd %d, IP %s, port %s",
                            *file_descriptor, ipstr, portstr);
                    #endif
                }
            }
            else if(ret == CLIENT_MANAGER_CLIENT_EXISTS)
            {
                /* Initialize the socket's thread */
                ret = CLIENT_NEW_SOCKET_CREATED;
            }
            
        }
    }

    return ret;
}




















void client_manager_free(client_manager_t *mgr)
{
    if(mgr != NULL)
    {
        /* ensure sockets are closed before vanishing */
        for(size_t i = 0; i < mgr->active_sockets_no; ++i)
        {
            if(mgr->list[i].fd >= 0)
            {
                /* TO DO:
                    close properly the clients, send them the appropriate message!
                */
                close(mgr->list[i].fd);
                log_info("ClientManager: closed client fd=%d", mgr->list[i].fd);
            }
        }
        free(mgr->list);
        free(mgr);
    }
}
void client_manager_set_pid(client_manager_t *mgr, pid_t pid, int file_descriptor)
{
    if(mgr != NULL && pid > 0 && file_descriptor >= 0)
    {
        for(size_t i = 0; i < mgr->active_sockets_no; ++i)
        {
            if(mgr->list[i].fd == file_descriptor)
            {
                mgr->list[i].pid = pid;
                log_info("ClientManager: associated pid %d with fd %d", pid, file_descriptor);
                return;
            }
        }
    }
    log_error("ClientManager: fd %d not found when assigning pid %d", file_descriptor, pid);
}

void client_manager_handle_socket(int socket_fd)
{
    /* check and call client's utilities */
    if(socket_fd > 0)
    {
        client_handle(socket_fd);
    }
}

void client_manager_reap(client_manager_t *mgr, pid_t dead)
{
    if(mgr != NULL && dead > 0)
    {
        /* loop through all acrive sockets */
        for(size_t i = 0; i < mgr->active_sockets_no; ++i)
        {
            /* if the element corresponds to the zombie */
            if(mgr->list[i].pid == dead)
            {
                log_info("ClientManager: reaping pid %d (fd=%d)", dead, mgr->list[i].fd);

                /* close socket */
                if(mgr->list[i].fd >= 0)
                {
                    close(mgr->list[i].fd);
                }

                /* remove element: swap with last for O(1) erase */
                mgr->list[i] = mgr->list[mgr->active_sockets_no - 1];
                mgr->active_sockets_no--;
                return;
            }
        }

        log_error("ClientManager: pid %d not found while reaping", dead);
    }
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int add_new_client(client_manager_t *mgr, const struct sockaddr *socket_address,
                               socklen_t socket_address_len, int file_descriptor)
{
    /* return variable */
    int ret = STATUS_FAILURE;

    /* check if the client manager is NULL */
    if(mgr == NULL || socket_address == NULL || file_descriptor < 0 || socket_address_len <= 0)
    {
        errno = EINVAL;
        log_error("ClientManager: add_new_client(): invalid input parameters %s", strerror(errno));
    }

    else
    {
         = 

    }


    return ret;
}
