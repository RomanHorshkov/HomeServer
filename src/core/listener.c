#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include "listener.h"  //  listener

#include <errno.h>   // errno, EADDRINUSE, etc.
#include <fcntl.h>   // fcntl(), F_GETFL, F_SETFL, O_NONBLOCK
#include <netdb.h>   // getaddrinfo(), addrinfo, gai_strerror()
#include <stdlib.h>  // malloc(), calloc() etc
#include <string.h>  // memset(), strcpy(), strlen(), etc.
#include <unistd.h>  // fork(), close(), read(), write(), etc.
#include <sys/epoll.h>   // epoll_create1(), epoll_ctl(), epoll_wait(), struct epoll_event
#include <arpa/inet.h>  // inet_ntop(), struct sockaddr_in, struct sockaddr_in6

#include "logger.h"           // logger
#include "server_settings.h"  // settings

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */

struct listener
{
    /* sockets file descriptors */
    int sockets_fds[MAX_LISTENERS];

    /* active listening sockets */
    int active_sockets_no;

    /* pipe write fd */
    int pipe_write_fd;
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

/* functions related to the creation of a listener */
static int create_listener(struct addrinfo *server_info_out, listener_t **listener_ptr);

static int init_memory(listener_t **listener_ptr);

static int set_socket_hints(struct addrinfo *hints);

static int set_listener_socket_reusability(const int *socket_fd);

static int set_listener_socket_restartability(const int *socket_fd);


static int set_listener_socket_options(const int *listener_socket_fd, const int32_t *ai_family);

/* functions related to the destruction of a listener */
static void listener_close(listener_t **listener_ptr);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int listener_init(listener_t **listener_ptr, const char *port, int *pipe_write_fd)
{
    /* return value */
    int res = STATUS_FAILURE;

    /* Hints for getaddrinfo research */
    struct addrinfo hints;

    /* getaddrinfo output info */
    struct addrinfo *server_info_out = NULL;

    /* Check input */
    if (port == NULL || pipe_write_fd == NULL)
    {
        log_error("listener_init invalid input");
    }

    /* Initialize memory */
    else if(init_memory(listener_ptr) != STATUS_SUCCESS)
    {
        log_error("listener memory initialization failed ", strerror(errno));
    }

    /* Set socket hints for getaddr */
    else if(set_socket_hints(&hints) != STATUS_SUCCESS)
    {
        log_error("listener socket hints failed ", strerror(errno));
    }

    /* get connectable sockets with hints */
    else if(getaddrinfo(NULL, port, &hints, &server_info_out) != 0)
    {
        log_error("listener getaddrinfo failed ", strerror(errno));
    }

    /* Create and start the listener sockets */
    else if(create_listener(server_info_out, listener_ptr) != STATUS_SUCCESS)
    {
        log_error("listener start failed ", strerror(errno));
    }

    /* Print info */
    else
    {
        /* Set the pipe write file descriptor */
        (*listener_ptr)->pipe_write_fd = *pipe_write_fd;

        log_info("getaddrinfo(NULL, %s) succeeded. Printing address info list results:\n", port);
        log_addrinfo_list(server_info_out);
        res = STATUS_SUCCESS;
    }

    /* now can freeup the memory allocated in server_info out */
    freeaddrinfo(server_info_out);
    return res;
}


void *listener_run(void *arg)
{
    log_info("Starting Listener...");

    /* Check input */
    if (arg == NULL)
    {
        log_error("listener_run invalid input");
    }

    /* main listener thread loop */
    else
    {

        listener_t *listener_ptr = (listener_t *)arg;

        int epfd = epoll_create1(0);
        struct epoll_event ev, events[MAX_LISTENERS];

        /* Register all listening sockets with epoll */
        for (int i = 0; i < listener_ptr->active_sockets_no; ++i) {
            ev.events = EPOLLIN;
            ev.data.fd = listener_ptr->sockets_fds[i];
            epoll_ctl(epfd, EPOLL_CTL_ADD, listener_ptr->sockets_fds[i], &ev);
        }

        while (1)
        {
            log_info("listener: waiting for incoming connections...");
            int nfds = epoll_wait(epfd, events, MAX_LISTENERS, -1);
            log_info("listener: epoll_wait returned %d events", nfds);

            if (nfds < 0)
            {
                log_error("listener: epoll_wait failed: %s", strerror(errno));
            }
            else
            {
                for (int i = 0; i < nfds; ++i)
                {
                    int listen_fd = events[i].data.fd;
                    struct sockaddr_storage client_addr;
                    socklen_t addrlen = sizeof(client_addr);
                    int client_fd = accept4(listen_fd, (struct sockaddr *)&client_addr, &addrlen, SOCK_NONBLOCK);
                    if (client_fd >= 0)
                    {
                        set_socket_non_blocking(&client_fd);

                        char ipstr[INET6_ADDRSTRLEN];
                        void *addr;
                        if (client_addr.ss_family == AF_INET)
                        {
                            addr = &((struct sockaddr_in *)&client_addr)->sin_addr;
                        }
                        else
                        {
                            addr = &((struct sockaddr_in6 *)&client_addr)->sin6_addr;
                        }
                        inet_ntop(client_addr.ss_family, addr, ipstr, sizeof(ipstr));
                        log_info("[listener] Accepted client from %s, fd %d", ipstr, client_fd);

                        /* Write the client_fd to the pipe for the worker */
                        write(listener_ptr->pipe_write_fd, &client_fd, sizeof(client_fd));
                    }
                }
            }
        }
    }
    return NULL;
}

static void listener_close(listener_t **listener_ptr)
{
    log_info("listener: closing all listening sockets");
    
    if (listener_ptr == NULL || *listener_ptr == NULL)
    {
        log_error("listener_close: invalid listener pointer");
    }

    /* loop through all listener's sockets */
    else
    {
        for (int i = 0; i < (*listener_ptr)->active_sockets_no; i++)
        {
            /* check if listener is active */
            if((*listener_ptr)->sockets_fds[i] > 0)
            {
                /* close listener file descriptor */
                close((*listener_ptr)->sockets_fds[i]);

                /* reset to 0 */
                (*listener_ptr)->sockets_fds[i] = 0;

                /* decrease listener number */
                (*listener_ptr)->active_sockets_no--;
            }
            else
            {
                continue;
            }
        }
    }
}

void listener_shutdown(listener_t **listener_ptr)
{
    log_info("Listeners: -- shutdown -- ");
    listener_close(listener_ptr);
    free(*listener_ptr);
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int create_listener(struct addrinfo *server_info_out, listener_t **listener_ptr)
{
    /* return value */
    int res = STATUS_FAILURE;

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
        else if(set_listener_socket_options(&listener_socket_fd, &server_info_out->ai_family) != STATUS_SUCCESS)
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

        /* check if available space for more sockets */
        else if ((*listener_ptr)->active_sockets_no >= MAX_LISTENERS)
        {
            log_error("Maximum number of listeners reached: %d", MAX_LISTENERS);
            /* delete the socket */
            close(listener_socket_fd);
        }

        /* If everything worked fine */
        {
            /* Put the socket in the listener */
            (*listener_ptr)->sockets_fds[(*listener_ptr)->active_sockets_no] = listener_socket_fd;

            /* Increase the counter */
            (*listener_ptr)->active_sockets_no++;

            /* Set return value to success */
            res = STATUS_SUCCESS;
        }

        /* TODO: check separately each listener, not in batch. */

        server_info_out = server_info_out->ai_next;
    }

    return res;
}

static int init_memory(listener_t **listener_ptr)
{
    /* return value */
    int res = STATUS_FAILURE;

    /* free listener's memory */
    if(*listener_ptr != NULL)
    {
        free(*listener_ptr);
    }
    *listener_ptr = NULL;

    /* allocate memory for the listener */
    *listener_ptr = (listener_t *)calloc(1, sizeof(listener_t));

    /* Check memory allocation */
    if(*listener_ptr != NULL)
    {
        res = STATUS_SUCCESS;
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
    int res = STATUS_FAILURE;

    if(hints)
    {
        /* make sure hints setted to 0 */
        memset(hints, 0, sizeof(struct addrinfo));

        /* AF_UNSPEC allows IPv4 or IPv6 */
        hints->ai_family = AF_UNSPEC;

        /* The sockettype will automatically set the ai_protocol to
        IPPROTO_TCP 6 with SOCK_STREAM or IPPROTO_UDP 17 with SOCK_DGRAM */
        hints->ai_socktype = SOCK_STREAM;  // TCP

        /* Use my IP automatically */
        hints->ai_flags = AI_PASSIVE;

        res = STATUS_SUCCESS;
    }

    return res;
}

static int set_listener_socket_reusability(const int *socket_fd)
{
    /* return value */
    int res = STATUS_FAILURE;

    /* Allow reuse of address (critical for restarts) */
    int yes = 1;

    if(setsockopt(*socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != -1)
    {
        res = STATUS_SUCCESS;
    }
    else
    {
        log_error("listener socket reusability failed to set: %s", strerror(errno));
    }

    return res;
}

static int set_listener_socket_restartability(const int *socket_fd)
{
    /* return value */
    int res = STATUS_FAILURE;

    /* Options for restart: Just kill it. Don’t wait. Don’t flush data.
    These options are ok for listeners. */
    struct linger sl;
    sl.l_onoff = 1;
    sl.l_linger = 0;
    if(setsockopt(*socket_fd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl)) != -1)
    {
        res = STATUS_SUCCESS;
    }
    else
    {
        log_error("listener socket restartability failed to set: %s", strerror(errno));
    }

    return res;
}

int set_socket_non_blocking(const int *socket_fd)
{
    /* return value */
    int res = STATUS_FAILURE;

    int flags = fcntl(*socket_fd, F_GETFL, 0);
    if(flags != -1)
    {
        if(fcntl(*socket_fd, F_SETFL, flags | O_NONBLOCK) != -1)
        {
            res = STATUS_SUCCESS;
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

static int set_listener_socket_options(const int *listener_socket_fd, const int32_t *ai_family)
{
    /* return value */
    int res = STATUS_FAILURE;

    if(set_listener_socket_reusability(listener_socket_fd) != STATUS_SUCCESS)
    {
        log_error("set_listener_socket_reusability failed.");
    }
    else if(set_listener_socket_restartability(listener_socket_fd) != STATUS_SUCCESS)
    {
        log_error("set_listener_socket_restartability failed.");
    }
    else if(set_socket_non_blocking(listener_socket_fd) != STATUS_SUCCESS)
    {
        log_error("set_socket_non_blocking failed.");
    }
    else
    {
        /* Set the return to Success */
        res = STATUS_SUCCESS;

        /* restrict the socket to only ipv6 if socket for ipv6 */
        if(*ai_family == AF_INET6)
        {
            /* yes value */
            int yes = 1;

            if(setsockopt(*listener_socket_fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes,
                            sizeof(yes)) != STATUS_SUCCESS)
            {
                res = STATUS_FAILURE;
                log_error("Socket ipv6 opts failed: %s\n", strerror(errno));
            }
        }
    }

    return res;
}
