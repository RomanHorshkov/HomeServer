#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE

#include <netdb.h>                  // socklen_t, NI_MAXHOST, NI_MAXSERV

#include "client.h"
#include "browser.h"
// #include "server_settings.h"
#include "logger.h"

#include <errno.h>                  // errno, EADDRINUSE, etc.
// #include <unistd.h>                 // ssize_t
#include <sys/socket.h>             // socklen_t, socket(), bind(), setsockopt(), etc.
#include <sys/time.h>               // struct timeval
#include <string.h>                 // memset(), strcpy(), strlen(), etc.
#include <stdlib.h>                 // malloc(), calloc() etc
#include <stdio.h>                  // snprintf(), etc


/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */




/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */

/* Server's clients structures */

typedef struct {
    int client_fd;                  // file descriptor
    int pid;                        // process id
    char host[NI_MAXHOST];          // host name
    char service[NI_MAXSERV];       // service name
} client_info;

struct clients_pot {
    client_info clients[MAX_CLIENTS]; // all clients
    int active_clients_no;            // active clients count
};

/****************************************************************************
 * PRIVATE FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

static int save_client(clients_t **clients, struct sockaddr *client_addr, int *client_fd);

static int set_client_socket_options(int *socket_fd);

static int upgrade_client_socket_options(int *socket_fd);

static void close_all_clients(clients_t **clients);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int clients_init(clients_t **clients)
{
    /* result value */
    int res = -1;

    if (clients != NULL)
    {
        /* Allocate space for clients */
        *clients = malloc(sizeof(clients_t));

        if (*clients != NULL)
        {
            /* Set everything to 0 */
            memset(*clients, 0, sizeof(clients_t));
            /* set return to 0 */
            res = 0;
        }
    }
    
    return res;
}

int clients_add_new_client(clients_t **clients, struct sockaddr_storage *client_addr, int *client_fd)
{
    /* return value */
    int res = 0;

    if( (*clients)->active_clients_no >= MAX_CLIENTS)
    {
        /* Max no of clients reached, politely decline connection */
        log_error("Clients: max client No limit reached ", strerror(errno));
        close(*client_fd);
        *client_fd = -1;
        res = -1;
    }

    /* set client socket options */
    else if(set_client_socket_options(client_fd) == -1)
    {
        log_error("Clients: set client socket options failed: %s", strerror(errno));
        close(*client_fd);
        *client_fd = -1;
        res = -1;
    }

    /* Save client into clients array */
    else if(save_client(clients, (struct sockaddr *)client_addr, client_fd) == -1)
    {   
        log_error("Clients: save client failed: %s", strerror(errno));
        close(*client_fd);
        *client_fd = -1;
        res = -1;
    }

    return res;
}

void clients_handle_client(int *client_fd)
{
    /* set a local file descriptor */
    int fd = *client_fd;

    /* set a connections counter */
    int received_first_time = 0;

    /* receive buffer */
    char recv_buff[HTTP_RECEIVE_BUFFER_LEN];
    /* receive buffer len */
    ssize_t n;

    /* send buffer */
    char send_buff[HTTP_SEND_BUFFER_LEN];

    /* Client's connection policy over http */
    int client_connection_policy = -1;

    /* recv is BLOCKING here */
    while((n = recv(fd, recv_buff, sizeof(recv_buff), 0)) > 0)
    {
        log_info("Client [pid %d fd %d]: received, parsing ->", getpid(), fd);

        /* serve the request if any and respond */
        size_t send_len = browser_manage_client_req(recv_buff, (size_t)n, send_buff, &client_connection_policy);

        if(send_len <= 0)
        {
            log_error("Client: browser failed to manage request", strerror(errno));
        }

        else if(send(fd, send_buff, (size_t)send_len, 0) < 0)
        {
            log_error("Client: failed to send() response", strerror(errno));
        }
        /* Check if the client wants the connection to close */
        else if (client_connection_policy == HTTP_CONNECTION_CLOSE)
        {
            log_info("Client [pid %d fd %d]: closes connection", getpid(), fd);
            break; /* exit and close the client */
        }
        

        /* Upgrade after first msg received the client's socket options */
        else if(!received_first_time && upgrade_client_socket_options(&fd) == 0)
        {
            received_first_time = 1;
            log_info("Upgraded timeout for fd=%d to %ds\n", fd, CLIENT_MAX_TIMEOUT_S_L);
        }
    }

    if(n == 0)
    {
        log_info("Client fd=%d closed connection.\n", fd);
    }
    else if(errno != EAGAIN && errno != EWOULDBLOCK)
    {
        log_error("recv: %s", strerror(errno));
    }

    
    /* Close client */
    close(fd);

    log_info("Client handeled; Connection closed.");
}


void clients_erase_client(clients_t **clients, pid_t *client_pid)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if ((*clients)->clients[i].pid == *client_pid)
        {
            log_info("Clients: client reaped: PID %d (fd=%d)",
                     (*clients)->clients[i].pid,
                     (*clients)->clients[i].client_fd);

            if ((*clients)->clients[i].client_fd != -1)
                {
                    /* if the socket is active close the
                    client and set file descriptor to -1 */
                    close((*clients)->clients[i].client_fd);
                }

            /* update client's internal values */
            memset(&(*clients)->clients[i], 0, sizeof((*clients)->clients[i]));
            (*clients)->clients[i].client_fd = -1;
            (*clients)->clients[i].pid = 0; // not necessary but for clarity

            /* decrease client's number */
            (*clients)->active_clients_no--;

            log_info("Clients: remaining clients: %d", (*clients)->active_clients_no);
            break; // no need to look further
        }
        else
        {
            /* try next client */
            continue;
        }
    }
}

// void clients_close_client(clients_t **clients, pid_t *client_fd)
// {
//     for (int i = 0; i < MAX_CLIENTS; i++)
//     {
//         if ((*clients)->clients[i].client_fd == *client_fd)
//         {
//             log_info("Client closed: PID %d (fd=%d)",
//                      (*clients)->clients[i].pid,
//                      (*clients)->clients[i].client_fd);

//             /* close the client */
//             close((*clients)->clients[i].client_fd);

//             /* Clear the client slot */
//             (*clients)->clients[i].client_fd = -1;
//             (*clients)->clients[i].pid = -1;
//         }
//         else
//         {
//             /* try next client */
//             continue;
//         }
//     }
// }

// void clients_close_all(clients_t **clients)
// {
//     for (int i = 0; i < MAX_CLIENTS; i++)
//     {
//         if ((*clients)->clients[i].client_fd > 0 )
//         {
//             /* close the client */
//             close((*clients)->clients[i].client_fd);
//         }
//     }
// }

void clients_set_client_pid(clients_t **clients, pid_t *pid, int *client_fd)
{
    /* loop through all clients */
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        /* if a corresponding found */
        if ((*clients)->clients[i].client_fd == *client_fd)
        {
            /* set client's pid */
            (*clients)->clients[i].pid = *pid;

            log_info("Clients: client started: PID %d; fd %d; host %s; service %s ",
                     *pid, *client_fd, (*clients)->clients[i].host, (*clients)->clients[i].service);
            
            break;
        }
    }
}

void clients_shutdown(clients_t **clients)
{
    close_all_clients(clients);
    free(*clients);
    log_info("Clients: -- shutdown -- ");
}


/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */



static int save_client(clients_t **clients, struct sockaddr *client_addr, int *client_fd)
{
    /* return value */
    int res = -1;

    /* input length */
    socklen_t addr_len = sizeof(*client_addr);

    /* host client's info */
    char host[NI_MAXHOST];

    /* service client's info */
    char service[NI_MAXSERV];

    /* get all client's info */
    int gni = getnameinfo(client_addr, addr_len,
                        host, sizeof(host),
                        service, sizeof(service),
                        NI_NUMERICHOST | NI_NUMERICSERV);


    if (gni != 0)
    {
        /* Could not resolve client info, fallback to unknown client */
        snprintf(host, sizeof(host), "unknown");
        snprintf(service, sizeof(service), "unknown");
        
        log_info("Client (fd=%d) accepted, but getnameinfo failed: %s",
                    *client_fd, gai_strerror(gni));
    }

    /* save the client */
    /* space is already allocated in clients_init */
    /* loop through all clients and find empty space */
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if ((*clients)->clients[i].client_fd <= 0)
        {
            /* if the i-th client slot is available (free) */
            (*clients)->clients[i].client_fd = *client_fd;
            memcpy((*clients)->clients[i].host,    host,    NI_MAXHOST);
            memcpy((*clients)->clients[i].service, service, NI_MAXSERV);

            /* increment active clients amount */
            (*clients)->active_clients_no++;

            /* set return value to success */
            res = 0;
            
            log_info("Clients: client accepted from %s:%s (fd=%d)",
                    host, service, *client_fd);
            log_info("Clients: active clients %d", (*clients)->active_clients_no);
            
            /* stop after one client saved */
            break;
        }
        else
        {
            /* check next available slot */
            continue;
        }
    }

    return res;
}

static int set_client_socket_options(int *socket_fd)
{
    /* return value */
    int res = -1;

    /* Timeout */
    struct timeval timeout = {CLIENT_MAX_TIMEOUT_S, CLIENT_MAX_TIMEOUT_MS};
    
    if(setsockopt(*socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        /* log error if set sock opt fails and return -1 */
        log_error("setsockopt timeout: %s\n", strerror(errno));
    }
    else
    {
        res = 0;
    }

    return res;
}
 
static int upgrade_client_socket_options(int *socket_fd)
{
    /* return value */
    int res = -1;

    /* Timeout */
    struct timeval timeout = {CLIENT_MAX_TIMEOUT_S_L, CLIENT_MAX_TIMEOUT_MS};
    
    if (setsockopt(*socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        log_error("setsockopt timeout: %s\n", strerror(errno));
    }
    else
    {
        res = 0;
    }
    return res;
}

static void close_all_clients(clients_t **clients)
{
    /* set active clients amount to 0 */
    (*clients)->active_clients_no = 0;

    /* loop through all clients */
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if ((*clients)->clients[i].client_fd > 0)
        {
            /* close the client */
            close((*clients)->clients[i].client_fd);

            /* leave the internal values for main process 
            clients handling. Close is needed to close the 
            connection but from this process never delete
            data about the client's socket fd */
            
            // /* update client's internal values */
            // (*clients)->clients[i].client_fd = 0;
            // (*clients)->clients[i].pid = -1;
            // memcpy((*clients)->clients[i].host,    NULL, NI_MAXHOST);
            // memcpy((*clients)->clients[i].service, NULL, NI_MAXSERV);
        }
        else
        {
            /* client not active */
            continue;
        }
    }
}