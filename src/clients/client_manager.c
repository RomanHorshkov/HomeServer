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
#include <sys/time.h>    // struct timeval
#include <unistd.h>      // close()

#include "client.h"
#include "logger.h"
#include "server_settings.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */

/* Server's clients structures */
struct socket_info
{
    int fd;                 // file descriptor
    pid_t pid;              // process id
    char host[NI_MAXHOST];  // host name
    char port[NI_MAXSERV];  // port name
};

struct client_manager
{
    struct socket_info *list;  // dynamic array
    size_t active_sockets_no;  // active clients count
    size_t sockets_list_size;  // clients list size
};

/****************************************************************************
 * PRIVATE FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief Re‑allocates the internal list to double elements.
 * @return 0 on success, -1 on error.
 */
static int expand_clients_array(struct client_manager *mgr);

static int check_clients_array(struct client_manager *mgr);

static void add_socket(struct client_manager *mgr, const struct sockaddr *socket_address,
                       socklen_t socket_address_len, int file_descriptor);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

client_manager_t *client_manager_new(size_t clients_initial_amount)
{
    /* return variable, client manager */
    struct client_manager *new_client_manager = calloc(1, sizeof(struct client_manager));

    /* set a minimum size for the clients amount */
    if(clients_initial_amount < MAX_CLIENTS)
    {
        clients_initial_amount = (size_t)MAX_CLIENTS;
    }

    if(new_client_manager != NULL)
    {
        /* allocate space for clients */
        new_client_manager->list = calloc(clients_initial_amount, sizeof(struct socket_info));

        /* check if allocation successful */
        if(new_client_manager->list != NULL)
        {
            new_client_manager->sockets_list_size = clients_initial_amount;
            log_info("ClientManager: created with initial capacity %zu", clients_initial_amount);
        }
        else
        {
            log_error("ClientManager: failed to allocate client list: %s", strerror(errno));
        }
    }
    else
    {
        log_error("ClientManager: failed to allocate manager: %s", strerror(errno));
    }

    return new_client_manager;
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

int client_manager_add_socket(client_manager_t *mgr, const struct sockaddr_storage *addr,
                              socklen_t addrlen, int file_descriptor)
{
    /* return variable */
    int ret = -1;

    /* check input and clients */
    if(mgr == NULL || file_descriptor < 0)
    {
        errno = EINVAL;
        log_error("ClientManager: client_manager_add_socket(): invalid input", strerror(errno));
    }
    else if(check_clients_array(mgr) == -1)
    {
        log_error("ClientManager: check_clients_array() failed miserably", strerror(errno));
    }

    else
    {
        /* everything went good,
        add the socket info to clients_manager */
        add_socket(mgr, (const struct sockaddr *)addr, addrlen, file_descriptor);
        ret = 0;
    }

    return ret;
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

static int expand_clients_array(struct client_manager *mgr)
{
    /* return variable */
    int ret = -1;

    /* new size (duplicate) */
    size_t new_size = mgr->sockets_list_size * 2;

    struct socket_info *new_list = realloc(mgr->list, new_size * sizeof(struct socket_info));

    /* check realloc success */
    if(new_list != NULL)
    {
        /* zero the new list */
        memset(new_list + mgr->sockets_list_size, 0,
               (new_size - mgr->sockets_list_size) * sizeof(struct socket_info));

        /* reassign the list */
        mgr->list = new_list;

        /* update the size */
        mgr->sockets_list_size = new_size;

        /* set return variable */
        ret = 0;
        log_info("Client Manager: Successfully expanded clients array to %ld", new_size);
    }
    else
    {
        log_error("Client Manager: realloc failed: %s", strerror(errno));
    }

    return ret;
}

static int check_clients_array(struct client_manager *mgr)
{
    /* return variable */
    int ret = -1;

    /* check size */
    if(mgr->active_sockets_no >= mgr->sockets_list_size)
    {
        /* Resize up the array (double it) */
        if(expand_clients_array(mgr) != -1)
        {
            ret = 0;
        }
    }
    else
    {
        /* nothing to do */
        ret = 0;
    }

    return ret;
}

static void add_socket(struct client_manager *mgr, const struct sockaddr *socket_address,
                       socklen_t socket_address_len, int file_descriptor)
{
    /* get the next free socket_info struct in the manager's list */
    struct socket_info *new_sock = &mgr->list[mgr->active_sockets_no];

    /* assign the file descriptor */
    new_sock->fd = file_descriptor;

    /* increase the total active sockets number */
    mgr->active_sockets_no++;

    int gntret =
        getnameinfo(socket_address, socket_address_len, new_sock->host, sizeof(new_sock->host),
                    new_sock->port, sizeof(new_sock->port), NI_NUMERICHOST | NI_NUMERICSERV);

    /* check if getnameinfo resolved correctly the socket information */
    if(gntret != 0)
    {
        /* write unknown to the socket's host and port */
        strncpy(new_sock->host, "unknown", sizeof(new_sock->host));
        strncpy(new_sock->port, "unknown", sizeof(new_sock->port));

        log_info("ClientManager: client (fd=%d) accepted, but getnameinfo failed: %s",
                 file_descriptor, gai_strerror(gntret));
    }

    log_info("ClientManager: added client fd=%d (%s:%s)", file_descriptor, new_sock->host,
             new_sock->port);
}
