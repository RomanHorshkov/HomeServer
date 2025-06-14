#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include "listener.h" /* listener */

#include <arpa/inet.h> /* inet_ntop(), struct sockaddr_in, struct sockaddr_in6 */
#include <errno.h>     /* errno, EADDRINUSE, etc. */
#include <fcntl.h>     /* fcntl(), F_GETFL, F_SETFL, O_NONBLOCK */
#include <netdb.h>     /* getaddrinfo(), addrinfo, gai_strerror() */
#include <stdatomic.h> /* atomic_int */
#include <stdlib.h>    /* malloc(), calloc(), free, etc. */
#include <string.h>    /* memset(), strcpy(), strlen(), etc. */
#include <sys/epoll.h> /* epoll_create1(), epoll_ctl(), epoll_wait(), struct epoll_event */
#include <unistd.h>    /* fork(), close(), read(), write(), etc. */

#include "logger.h"          /* logger */
#include "server_settings.h" /* settings */

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

    /* status variable */
    atomic_int status; /* 0 = inactive, 1 = active */
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

/**
 * @brief Create and bind all listening sockets for the server.
 *
 * Iterates through the provided addrinfo list, creating, configuring, binding,
 * and listening on each socket. Successfully created sockets are stored in the
 * listener instance. Supports both IPv4 and IPv6 as available.
 *
 * @param server_info_out   Linked list of addrinfo structures from getaddrinfo().
 * @param listener_ptr      Address of a pointer to the listener instance.
 * @retval  0  Success (at least one socket created and listening).
 * @retval -1 Failure (no sockets created; see log for details).
 */
static int create_listener(struct addrinfo *server_info_out, listener_t **listener_ptr);

/**
 * @brief Allocate and initialize memory for a new listener instance.
 *
 * Frees any existing listener instance pointed to by @p listener_ptr, then
 * allocates and zero-initializes a new listener structure.
 *
 * @param listener_ptr  Address of a pointer to the listener instance.
 * @retval  0  Success.
 * @retval -1 Failure (memory allocation failed).
 */
static int init_memory(listener_t **listener_ptr);

/**
 * @brief Set recommended socket options in the addrinfo hints structure.
 *
 * Initializes the provided hints structure for getaddrinfo() with recommended
 * values for dual-stack TCP listening sockets.
 *
 * @param hints  Pointer to the addrinfo structure to initialize.
 * @retval  0  Success.
 * @retval -1 Failure (invalid pointer).
 */
static int set_socket_hints(struct addrinfo *hints);

/**
 * @brief Enable address reuse on a socket.
 *
 * Sets the SO_REUSEADDR option on the provided socket file descriptor,
 * allowing the server to restart without waiting for old sockets to time out.
 *
 * @param socket_fd  Pointer to the socket file descriptor.
 * @retval  0  Success.
 * @retval -1 Failure (setsockopt failed).
 */
static int set_listener_socket_reusability(const int *socket_fd);

/**
 * @brief Enable fast restart on a socket by setting SO_LINGER.
 *
 * Configures the socket to discard unsent data and close immediately on shutdown,
 * which is suitable for listener sockets.
 *
 * @param socket_fd  Pointer to the socket file descriptor.
 * @retval  0  Success.
 * @retval -1 Failure (setsockopt failed).
 */
static int set_listener_socket_restartability(const int *socket_fd);

/**
 * @brief Apply all recommended socket options for a listener socket.
 *
 * Sets SO_REUSEADDR, SO_LINGER, and O_NONBLOCK on the provided socket, and
 * restricts IPv6 sockets to IPv6-only if required.
 *
 * @param listener_socket_fd  Pointer to the socket file descriptor.
 * @param ai_family          Pointer to the address family (AF_INET/AF_INET6).
 * @retval  0  Success.
 * @retval -1 Failure (one or more options failed).
 */
static int set_listener_socket_options(const int *listener_socket_fd, const int32_t *ai_family);

/**
 * @brief Close all active listening sockets and reset the listener state.
 *
 * Iterates through all sockets in the listener, closes them, and resets the
 * socket count to zero. Safe to call multiple times.
 *
 * @param listener_ptr  Address of a pointer to the listener instance.
 */
static void listener_close(listener_t **listener_ptr);

/**
 * @brief Shutdown the listener and release all associated resources.
 *
 * Closes all sockets and frees the listener structure. Should be called when
 * the listener thread is exiting.
 *
 * @param listener_ptr  Address of a pointer to the listener instance.
 */
static void listener_shutdown(listener_t **listener_ptr);

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
    if(port == NULL || pipe_write_fd == NULL)
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

        /* Set the status to on */
        atomic_store(&(*listener_ptr)->status, SERVER_STATUS_ACTIVE);
        (*listener_ptr)->status = SERVER_STATUS_ACTIVE;

        res = STATUS_SUCCESS;

        log_info("getaddrinfo(NULL, %s) succeeded. Printing address info list results:\n", port);
        log_addrinfo_list(server_info_out);
    }

    /* now can freeup the memory allocated in server_info out */
    freeaddrinfo(server_info_out);
    return res;
}

void *listener_run(void *arg)
{
    log_info("Starting Listener...");

    /* Check input */
    if(arg == NULL)
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
        for(int i = 0; i < listener_ptr->active_sockets_no; ++i)
        {
            ev.events = EPOLLIN;
            ev.data.fd = listener_ptr->sockets_fds[i];
            epoll_ctl(epfd, EPOLL_CTL_ADD, listener_ptr->sockets_fds[i], &ev);
        }

        /* While the listener is active */
        while(atomic_load(&listener_ptr->status) == SERVER_STATUS_ACTIVE)
        {
#ifdef DEBUG_MODE
            log_info("listener: waiting for incoming connections...");
#endif /* DEBUG_MODE */

            /* Wait for incoming connections */
            int nfds = epoll_wait(epfd, events, MAX_LISTENERS, -1);

#ifdef DEBUG_MODE
            log_info("listener: epoll_wait returned %d events", nfds);
#endif /* DEBUG_MODE */

            if(nfds < 0)
            {
                log_error("listener: epoll_wait failed: %s", strerror(errno));
            }
            else
            {
                for(int i = 0; i < nfds; ++i)
                {
                    int listen_fd = events[i].data.fd;
                    struct sockaddr_storage client_addr;
                    socklen_t addrlen = sizeof(client_addr);
                    int client_fd = accept4(listen_fd, (struct sockaddr *)&client_addr, &addrlen,
                                            SOCK_NONBLOCK);
                    if(client_fd >= 0)
                    {
                        set_socket_non_blocking(&client_fd);

                        char ipstr[INET6_ADDRSTRLEN];
                        void *addr;
                        if(client_addr.ss_family == AF_INET)
                        {
                            addr = &((struct sockaddr_in *)&client_addr)->sin_addr;
                        }
                        else
                        {
                            addr = &((struct sockaddr_in6 *)&client_addr)->sin6_addr;
                        }
                        inet_ntop(client_addr.ss_family, addr, ipstr, sizeof(ipstr));
#ifdef DEBUG_MODE
                        log_info("[listener] Accepted client from %s, fd %d", ipstr, client_fd);
#endif /* DEBUG_MODE */
                        /* Write the client_fd to the pipe for the worker */
                        write(listener_ptr->pipe_write_fd, &client_fd, sizeof(client_fd));
                    }
                }
            }
        }

        /* if got out of the while loop then switching off is required */
        listener_shutdown(&listener_ptr);

        /* Close the epoll file descriptor */
        close(epfd);
    }
    return NULL;
}

void listener_set_status(listener_t *listener_ptr, int status)
{
    if(listener_ptr == NULL)
    {
        log_error("listener_set_status: invalid listener pointer");
    }
    else
    {
        /* Set the status */
        atomic_store(&listener_ptr->status, status);

#ifdef DEBUG_MODE
        /* Log the status change */
        log_info("Listener status set to %d", status);
#endif /* DEBUG_MODE */
    }
}

static void listener_close(listener_t **listener_ptr)
{
#ifdef DEBUG_MODE
    log_info("[listener]: listener_close: closing all listening sockets");
#endif /* DEBUG_MODE */
    if(listener_ptr == NULL || *listener_ptr == NULL)
    {
        log_error("[listener]: listener_close: invalid listener pointer");
    }

    /* loop through all listener's sockets */
    else
    {
        for(int i = 0; i < (*listener_ptr)->active_sockets_no; i++)
        {
            /* check if listener is active */
            if((*listener_ptr)->sockets_fds[i] > 0)
            {
                /* close listener file descriptor */
                close((*listener_ptr)->sockets_fds[i]);

                /* reset to 0 */
                (*listener_ptr)->sockets_fds[i] = 0;
            }
            else
            {
                continue;
            }
        }

        /* reset listener sockets number */
        (*listener_ptr)->active_sockets_no = 0;
    }
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
        else if(set_listener_socket_options(&listener_socket_fd, &server_info_out->ai_family) !=
                STATUS_SUCCESS)
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
        else if((*listener_ptr)->active_sockets_no >= MAX_LISTENERS)
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

            if(setsockopt(*listener_socket_fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes)) !=
               STATUS_SUCCESS)
            {
                res = STATUS_FAILURE;
                log_error("Socket ipv6 opts failed: %s\n", strerror(errno));
            }
        }
    }

    return res;
}

static void listener_shutdown(listener_t **listener_ptr)
{
#ifdef DEBUG_MODE
    log_info("[listener]: shutting down...");
#endif /* DEBUG_MODE */
    listener_close(listener_ptr);
    free(*listener_ptr);
}
