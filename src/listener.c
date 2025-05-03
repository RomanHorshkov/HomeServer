#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include "listener.h"  //  listener

#include <errno.h>   // errno, EADDRINUSE, etc.
#include <fcntl.h>   // fcntl(), F_GETFL, F_SETFL, O_NONBLOCK
#include <netdb.h>   // getaddrinfo(), addrinfo, gai_strerror()
#include <stdlib.h>  // malloc(), calloc() etc
#include <string.h>  // memset(), strcpy(), strlen(), etc.
#include <unistd.h>  // fork(), close(), read(), write(), etc.

#include "logger.h"           // logger
#include "server_settings.h"  // settings

/****************************************************************************
 * PUBLIC STUCTURED VARIABLES
 ****************************************************************************
 */

struct Listener
{
    int listeners_fds[MAX_LISTENERS];
    int active_listeners_no;
};

/****************************************************************************
 * PRIVATE VARIABLE DECLARATIONS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

static int start_listener(struct addrinfo *server_info_out, Listener_t **listener_ptr);

static int init_memory(Listener_t **listener);

static int set_socket_hints(struct addrinfo *hints);

static int set_listener_socket_reusability(int *socket_fd);

static int set_listener_socket_restartability(int *socket_fd);

static int set_listener_socket_non_blocking(int *socket_fd);

static int set_listener_socket_options(int *socket_fd, int32_t *ai_family);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int listener_init(Listener_t **listener_ptr, const char *port)
{
    /* return value of the function */
    int res = -1;

    /* Hints for getaddrinfo research */
    struct addrinfo hints;

    /* getaddrinfo output info */
    struct addrinfo *server_info_out;

    /* check if memory properly initialised */
    if(init_memory(listener_ptr) == -1)
    {
        log_error("listener memory initialization failed ", strerror(errno));
    }

    /* check if socket hints for getaddr are created */
    else if(set_socket_hints(&hints) == -1)
    {
        log_error("listener socket hints failed ", strerror(errno));
    }

    /* check if getaddrinfo obtains some sockets */
    else if(getaddrinfo(NULL, port, &hints, &server_info_out) != 0)
    {
        log_error("listener getaddrinfo failed ", strerror(errno));
    }

    /* check if listener starts */
    else if(start_listener(server_info_out, listener_ptr) == -1)
    {
        log_error("listener start failed ", strerror(errno));
    }

    /* Print info */
    else
    {
        log_info("getaddrinfo succeeded. Printing address info list results:\n");
        log_addrinfo_list(server_info_out);
        res = 0;
    }

    /* now can freeup the memory allocated in server_info out */
    freeaddrinfo(server_info_out);
    return res;
}

int listener_check_incoming_clients(Listener_t **listener, struct sockaddr_storage *client_addr,
                                    int *client_fd)
{
    /* TO DO:
    try to loop through just active_listeners
    but be sure they're always at the beginning */

    /* result value */
    int res = -1;

    /* temporary listener_fd */
    int listener_fd_tmp;

    /* temporary client_fd */
    int client_fd_tmp;

    /* input length */
    socklen_t addr_len = sizeof(*client_addr);

    /* Loop through all the listeners */
    for(int i = 0; i < MAX_LISTENERS; i++)
    {
        listener_fd_tmp = (*listener)->listeners_fds[i];

        /* Check if listener on */
        if(listener_fd_tmp <= 0)
        {
            /* the listener is not active */
            continue;
        }

        /* Accept incoming client request if any */
        client_fd_tmp = accept(listener_fd_tmp, (struct sockaddr *)client_addr, &addr_len);

        /* accept is set to not blocking:
        will return errno EAGAIN or EWOULDBLOCK if no incoming connections */

        /* if a valid client_fd was obtained */
        if(client_fd_tmp != -1)
        {
            /* set return value to success */
            res = 0;

            /* set [in] client_fd */
            *client_fd = client_fd_tmp;

            break;
        }
        else
        {
            /* An error occurred */
            if(errno != EAGAIN && errno != EWOULDBLOCK)
            {
                /* Real error: close and log client_fd */
                close(client_fd_tmp);
                log_error("Listener: accept() client failed %s", strerror(errno));
            }
            else
            {
                /* No pending client connections, continue */
            }
        }
    }

    return res;
}

void listener_close(Listener_t **listener)
{
    log_info("Listener: closing listeners' sockets");

    /* loop through all possible listeners */
    for(int i = 0; i < MAX_LISTENERS; i++)
    {
        /* check if listener is active */
        if((*listener)->listeners_fds[i] > 0)
        {
            /* close listener file descriptor */
            close((*listener)->listeners_fds[i]);

            /* reset to 0 */
            (*listener)->listeners_fds[i] = 0;

            /* decrease listener number */
            (*listener)->active_listeners_no--;
        }
        else
        {
            continue;
        }
    }
}

void listener_shutdown(Listener_t **listener)
{
    log_info("Listeners: -- shutdown -- ");
    listener_close(listener);
    free(*listener);
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int start_listener(struct addrinfo *server_info_out, Listener_t **listener_ptr)
{
    /* return value */
    int res = -1;

    /* loop through all the getaddrinfo output results */
    while(server_info_out != NULL)
    {
        /* Create a socket */
        int listener_socket_fd = socket(server_info_out->ai_family, server_info_out->ai_socktype,
                                        server_info_out->ai_protocol);

        if(listener_socket_fd == -1)
        {
            log_error("socket creation failed: %s\n", strerror(errno));
        }

        /* Set socket options */
        else if(set_listener_socket_options(&listener_socket_fd, &server_info_out->ai_family) == -1)
        {
            log_error("setsockopt failed: %s\n", strerror(errno));
            /* delete the socket */
            close(listener_socket_fd);
        }

        /* Bind the socket */
        else if(bind(listener_socket_fd, server_info_out->ai_addr, server_info_out->ai_addrlen) ==
                -1)
        {
            log_error("bind failed: %s\n", strerror(errno));
            /* delete the socket */
            close(listener_socket_fd);
        }

        /* Start listening */
        else if(listen(listener_socket_fd, MAX_PENDING_CONNECTIONS) == -1)
        {
            log_error("listen failed: %s\n", strerror(errno));
            /* delete the socket */
            close(listener_socket_fd);
        }

        /* If all worked fine */
        else
        {
            /* Put the socket in the listener and increase the counter */
            (*listener_ptr)->listeners_fds[(*listener_ptr)->active_listeners_no++] =
                listener_socket_fd;

            /* Set return value to success */
            res = 0;
        }

        server_info_out = server_info_out->ai_next;
    }

    return res;
}

static int init_memory(Listener_t **listener_ptr)
{
    /* return value */
    int res = -1;

    /* free listener's memory */
    if(*listener_ptr)
    {
        free(*listener_ptr);
    }
    *listener_ptr = NULL;

    /* allocate fresh memory for the listener */
    *listener_ptr = (Listener_t *)malloc(sizeof(Listener_t));

    if(*listener_ptr != NULL)
    {
        /* Set listeners to 0 */
        memset(*listener_ptr, 0, sizeof(Listener_t));
        if(*listener_ptr != NULL)
        {
            res = 0;
        }
        else
        {
            log_error("listener memory zeroing failed", strerror(errno));
        }
    }
    else
    {
        log_error("listener memory allocation failed", strerror(errno));
    }

    return res;
}

static int set_socket_hints(struct addrinfo *hints)
{
    /* return value */
    int res = -1;

    /* make sure hints setted to 0 */
    memset(hints, 0, sizeof(struct addrinfo));

    if(hints)
    {
        /* AF_UNSPEC allows IPv4 or IPv6 */
        hints->ai_family = AF_UNSPEC;

        /* The sockettype will automatically set the ai_protocol to
        IPPROTO_TCP 6 with SOCK_STREAM or IPPROTO_UDP 17 with SOCK_DGRAM */
        hints->ai_socktype = SOCK_STREAM;  // TCP

        /* Use my IP automatically */
        hints->ai_flags = AI_PASSIVE;

        res = 0;
    }

    return res;
}

static int set_listener_socket_reusability(int *socket_fd)
{
    /* return value */
    int res = -1;

    /* Allow reuse of address (critical for restarts) */
    int yes = 1;

    if(setsockopt(*socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != -1)
    {
        res = 0;
    }
    else
    {
        log_error("listener socket reusability failed to set: %s", strerror(errno));
    }

    return res;
}

static int set_listener_socket_restartability(int *socket_fd)
{
    /* return value */
    int res = -1;

    /* Options for restart: Just kill it. Don’t wait. Don’t flush data.
    These options are ok for listeners. */
    struct linger sl;
    sl.l_onoff = 1;
    sl.l_linger = 0;
    if(setsockopt(*socket_fd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl)) != -1)
    {
        res = 0;
    }
    else
    {
        log_error("listener socket restartability failed to set: %s", strerror(errno));
    }

    return res;
}

static int set_listener_socket_non_blocking(int *socket_fd)
{
    /* return value */
    int res = -1;

    int flags = fcntl(*socket_fd, F_GETFL, 0);
    if(flags != -1)
    {
        if(fcntl(*socket_fd, F_SETFL, flags | O_NONBLOCK) != -1)
        {
            res = 0;
        }
        else
        {
            log_error("fcntl set listener flags failed: %s\n", strerror(errno));
        }
    }
    else
    {
        log_error("fcntl get listener flags failed: %s\n", strerror(errno));
    }

    return res;
}

static int set_listener_socket_options(int *socket_fd, int32_t *ai_family)
{
    /* return value */
    int ret = -1;
    /* yes value */
    int yes = 1;

    if(set_listener_socket_reusability(socket_fd) != -1)
    {
        if(set_listener_socket_restartability(socket_fd) != -1)
        {
            if(set_listener_socket_non_blocking(socket_fd) != -1)
            {
                /* Set the return to 0 */
                ret = 0;
                /* restrict the socket to only ipv6 if socket for ipv6 */
                if(*ai_family == AF_INET6)
                {
                    if(setsockopt(*socket_fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes)) == -1)
                    {
                        ret = -1;
                        log_error("Socket ipv6 opts failed: %s\n", strerror(errno));
                    }
                }
            }
        }
    }

    return ret;
}