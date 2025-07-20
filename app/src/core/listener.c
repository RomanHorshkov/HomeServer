/**
 * @file listener.c
 * @brief Accepts and manages incoming connections, passing them to the worker.
 *
 * This module binds, listens, and accepts connections on IPv4/IPv6 sockets. It
 * sets options for reuse, linger, and non-blocking mode. When a new connection
 * arrives, the file descriptor is sent to the worker via a pipe.
 *
 * Usage:
 *   listener_init(...)
 *   listener_run(...)
 *
 * Exit Codes:
 *   STATUS_SUCCESS  (0)
 *   STATUS_FAILURE  (1)
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑05‑11
 * (c) 2025
 */

#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE   /* for accept4 */
#include "listener.h" /* listener */

#include <arpa/inet.h>   /* inet_ntop(), struct sockaddr_in, struct sockaddr_in6 */
#include <errno.h>       /* errno, EADDRINUSE, etc. */
#include <fcntl.h>       /* fcntl(), F_GETFL, F_SETFL, O_NONBLOCK */
#include <netdb.h>       /* getaddrinfo(), addrinfo, gai_strerror() */
#include <netinet/tcp.h> /* TCP_NODELAY */
#include <stdatomic.h>   /* atomic_int */
#include <stdlib.h>      /* malloc(), calloc(), free, etc. */
#include <string.h>      /* memset(), strcpy(), strlen(), etc. */
#include <unistd.h>      /* fork(), close(), read(), write(), etc. */

#include "logger.h"          /* logger */
#include "server_settings.h" /* settings */
#include "utils.h"           /* socket helper */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */

/* Define a handy alias for the enum listener_status in atomic version */
typedef _Atomic listener_status atomic_listener_status_t;

struct listener
{
    /* Port */
    char port[20];

    /* Listening sockets file descriptors */
    int sockets_fds[MAX_LISTENERS];

    /* Total active listening sockets */
    uint active_sockets_no;

    /* epoll instance */
    int epoll_fd;

    /* Listener status variable */
    atomic_listener_status_t status;

    /* collaboration with worker structure */
    pipeline_t *pipeline;

    /* worker last known status */
    worker_status status_worker;
};

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Create and bind all listening sockets for the server.
 *
 * Iterates through the provided addrinfo list, creating, configuring, binding,
 * and listening on each socket. Successfully created sockets are stored in the
 * listener instance. Supports both IPv4 and IPv6 as available.
 *
 * @param listener_ptr      Address of a pointer to the listener instance.
 * @param port              Pointer to the port char arr.
 * @retval  0  Success (at least one socket created and listening).
 * @retval -1 Failure (no sockets created; see log for details).
 */
static int init_listening_sockets(listener_t *listener_ptr, const char *port);

/**
 * @brief Close all active listening sockets and reset the listener state.
 *
 * Iterates through all sockets in the listener, closes them, and resets the
 * socket count to zero. Safe to call multiple times.
 *
 * @param listener_ptr  Address of a pointer to the listener instance.
 */
static int stop_listener(listener_t *listener_ptr);

#if 0
static int pause_listening(listener_t *listener_ptr);

static int resume_listening(listener_t *listener_ptr);

#endif

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

static int init_pipeline(listener_t *listener_ptr, pipeline_t *pipeline_ptr);

static int init_epoll_instance(listener_t *listener_ptr);

static int register_listener_sockets_to_epoll(listener_t *listener_ptr);

static int read_worker_message(listener_t *listener_ptr);

static int on_worker_status_change(listener_t *listener_ptr, worker_status worker_actual_status);

/**
 * @brief Shutdown the listener and release all associated resources.
 *
 * Closes all sockets and frees the listener structure. Should be called when
 * the listener thread is exiting.
 *
 * @param listener_ptr  Pointer to the listener instance.
 */
static void listener_shutdown(listener_t *listener_ptr);

#ifdef DEBUG_MODE
/**
 * @brief
 */
static int listener_accept_and_p_client_info(int listen_fd);
#endif /* DEBUG_MODE */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int listener_init(listener_t **listener_ptr, const char *port, pipeline_t *pipeline_ptr)
{
#ifdef DEBUG_MODE
    log_info("[listener] listener_init");
#endif /* DEBUG_MODE */
    /* return value */
    int res = STATUS_FAILURE;

    /* Check input */
    if(listener_ptr == NULL)
    {
        log_error("[listener] listener_init: listener_ptr is NULL");
    }

    else if(port == NULL)
    {
        log_error("[listener] listener_init: port is NULL");
    }

    else if(pipeline_ptr == NULL)
    {
        log_error("[listener] listener_init: pipeline_ptr is NULL");
    }

    /* Initialize memory */
    else if(init_memory(listener_ptr) != STATUS_SUCCESS)
    {
        log_error("[listener] listener_init: memory initialization failed ", strerror(errno));
    }

    /* Initialize worker communication pipeline */
    else if(init_pipeline(*listener_ptr, pipeline_ptr) != STATUS_SUCCESS)
    {
        log_error("[listener] listener_init: pipeline initialization failed ", strerror(errno));
    }

    /* Create and start the listener sockets */
    else if(init_listening_sockets(*listener_ptr, port) != STATUS_SUCCESS)
    {
        log_error("listener start failed ", strerror(errno));
    }

    /* Initialize epoll instance */
    else if(init_epoll_instance(*listener_ptr) != STATUS_SUCCESS)
    {
        log_error("[listener] epoll instance initialization failed ", strerror(errno));
    }

    /* If everything succceded */
    else
    {
        /* Set the pipeline collaboration structure */
        (*listener_ptr)->pipeline = pipeline_ptr;

        /* Set the status to on */
        listener_set_status(*listener_ptr, LISTENER_STATUS_ACTIVE);

        res = STATUS_SUCCESS;
    }
    return res;
}

void *listener_run(void *arg)
{
    /* Check input */
    if(arg == NULL)
    {
        log_error("listener_run invalid input");
    }

    else
    {
        listener_t *listener_ptr = (listener_t *)arg;

        worker_status worker_old_status = (worker_status)WORKER_STATUS_ACTIVE;
        worker_status worker_actual_status = (worker_status)WORKER_STATUS_ACTIVE;

        /* main listener thread loop */
        while(atomic_load(&listener_ptr->status) == LISTENER_STATUS_ACTIVE)
        {
            epoll_events_t evnts;
            /* Wait for epoll events on pipe / listener sockets */
            int nfds = epoll_listen_events(listener_ptr->epoll_fd, &evnts);

            if(nfds < 0)
            {
                log_error("listener: epoll_wait failed: %s", strerror(errno));
                continue;
            }

            for(int i = 0; i < nfds; ++i)
            {
                int listen_fd = epoll_get_event_fd(&evnts, i);

                /* Check if the worker on pipe has something to say */
                if(listen_fd == listener_ptr->pipeline->pipe_fds[0])
                {
                    worker_actual_status = read_worker_message(listener_ptr);

                    /* Check if a status change occurred */
                    if((worker_actual_status != worker_old_status) &&
                       (worker_actual_status != WORKER_STATUS_INVALID))
                    {
#ifdef DEBUG_MODE
                        log_info("[listener] worker_actual_status[%d] != worker_old_status[%d]",
                                 worker_actual_status, worker_old_status);
#endif /* DEBUG_MODE */
                        on_worker_status_change(listener_ptr, worker_actual_status);

                        /* Update the old status */
                        worker_old_status = worker_actual_status;
                    }
                    continue;
                }

                /* Not a pipe so a listener catched a new connecting client */
                else
                {
                    int client_fd = -1;
                    switch(worker_actual_status)
                    {
                        case WORKER_STATUS_ACTIVE:
                            /**
                             * Pulls the connection from the completed handshake queue.
                             * Returns a new file descriptor (client_fd) for that client connection.
                             * No new packet is sent at that exact moment — the handshake is already
                             * done. the application is now ready to read/write on client_fd.
                             */
#ifdef DEBUG_MODE
                            client_fd = listener_accept_and_p_client_info(listen_fd);
#else
                            client_fd = accept(listen_fd, NULL, NULL);
#endif /* DEBUG_MODE */

                            if(client_fd >= 0)
                            {
                                /* Set socket settings */
                                client_socket_init(&client_fd);

                                /* Push to ring and notify worker */
                                if(pipeline_push_and_notify_worker(listener_ptr->pipeline,
                                                                   client_fd) != STATUS_SUCCESS)
                                {
                                    close(client_fd);
                                    log_error("[listener] pipeline_push_and_notify_worker FAILED");
                                }
                            }
                            else
                            {
                                log_error("[listener] Failed to accept client: %s",
                                          strerror(errno));
                            }

                            break;

                        case WORKER_STATUS_FULL:

#ifdef DEBUG_MODE
                            log_info("[listener] worker status FULL... ");
#endif /* DEBUG_MODE */
                            /* Should proceed here and lock the listeners */
                            break;

                        default:

#ifdef DEBUG_MODE
                            log_info("[listener] worker status UNKNOWN %d", worker_actual_status);
#endif /* DEBUG_MODE */
                            break;
                    }
                }
            }
        } /* while(1) */

        /* Cleanup */
        listener_shutdown(listener_ptr);
    }
#ifdef DEBUG_MODE
    log_info("[listener] Listener thread exiting.");
#endif /* DEBUG_MODE */

    return NULL;
}

void listener_set_status(listener_t *listener_ptr, int status)
{
    /* check input */
    if(listener_ptr == NULL)
    {
        log_error("listener_set_status: invalid listener pointer");
    }

    else if(status >= LISTENER_STATUS_INVALID)
    {
        log_error("listener_set_status: invalid status value %d", status);
    }

    /* Set the status */
    else
    {
        atomic_store(&listener_ptr->status, status);

#ifdef DEBUG_MODE
        log_info("[listener] status set to %d", status);
#endif /* DEBUG_MODE */
    }
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int init_memory(listener_t **listener_ptr)
{
    /* return value */
    int res = STATUS_FAILURE;

    if(listener_ptr != NULL)
    {
        /* allocate memory for the listener */
        *listener_ptr = (listener_t *)calloc(1, sizeof(listener_t));

        /* Check memory allocation */
        if(*listener_ptr != NULL)
        {
            res = STATUS_SUCCESS;
#ifdef DEBUG_MODE
            log_info("[listener] memory setted up correctly");
#endif /* DEBUG_MODE */
        }

        else
        {
            log_error("[listener] memory allocation failed calloc", strerror(errno));
        }
    }

    else
    {
        log_error("[listener] allocation failed, invalid input", strerror(errno));
    }

    return res;
}

static int init_pipeline(listener_t *listener_ptr, pipeline_t *pipeline_ptr)
{
    /* return value */
    int res = STATUS_FAILURE;

    if((listener_ptr != NULL) && (pipeline_ptr != NULL))
    {
        listener_ptr->pipeline = pipeline_ptr;
        res = STATUS_SUCCESS;
    }

    else
    {
        log_error("[listener] init_pipeline failed, invalid input", strerror(errno));
    }

    return res;
}

static int init_listening_sockets(listener_t *listener_ptr, const char *port)
{
    /* return value */
    int res = STATUS_FAILURE;

    /* Hints for getaddrinfo research */
    struct addrinfo hints;

    /* getaddrinfo output info */
    struct addrinfo *server_info_out = NULL;

    /* Set listener's port */
    strncpy(listener_ptr->port, port, sizeof(listener_ptr->port) - 1);
    listener_ptr->port[sizeof(listener_ptr->port) - 1] = '\0';  // null-terminate manually

    /* Set socket hints for getaddr */
    if(socket_set_listener_hints(&hints) != STATUS_SUCCESS)
    {
        log_error("[listener] init_listening_sockets socket hints failed ", strerror(errno));
    }

    /* get connectable sockets with hints */
    else if(getaddrinfo(NULL, port, &hints, &server_info_out) != 0)
    {
        log_error("[listener] init_listening_sockets getaddrinfo failed ", strerror(errno));
    }

    else
    {
#ifdef DEBUG_MODE
        log_info(
            "[listener] getaddrinfo(NULL, %s) succeeded. Printing address info list results:\n",
            port);
        log_addrinfo_list(server_info_out);
#endif

        /* loop through all the getaddrinfo output results */
        while(server_info_out != NULL)
        {
            /* Reset the return variable */
            res = STATUS_FAILURE;

            /* Create a socket */
            int listener_socket_fd =
                socket(server_info_out->ai_family, server_info_out->ai_socktype,
                       server_info_out->ai_protocol);

            if(listener_socket_fd == -1)
            {
                log_error("[listener] socket creation failed: %s\n", strerror(errno));
            }

            /* check if available space for more sockets */
            else if(listener_ptr->active_sockets_no >= MAX_LISTENERS)
            {
                log_error("[listener] Maximum number of listeners reached: %d", MAX_LISTENERS);
                /* delete the socket */
                close(listener_socket_fd);
            }

            /* Set socket options */
            else if(listener_socket_init(&listener_socket_fd, &server_info_out->ai_family) !=
                    STATUS_SUCCESS)
            {
                log_error("[listener] setsockopt failed: %s\n", strerror(errno));
                /* delete the socket */
                close(listener_socket_fd);
            }

            /* Bind the socket */
            else if(bind(listener_socket_fd, server_info_out->ai_addr,
                         server_info_out->ai_addrlen) == -1)
            {
                log_error("[listener] bind failed: %s\n", strerror(errno));
                /* delete the socket */
                close(listener_socket_fd);
            }

            /* Start listening */
            else if(listen(listener_socket_fd, MAX_PENDING_CONNECTIONS) == -1)
            {
                log_error("[listener] listen failed: %s\n", strerror(errno));
                /* delete the socket */
                close(listener_socket_fd);
            }

            /* If everything worked fine */
            else
            {
                /* Put the socket in the listener */
                listener_ptr->sockets_fds[listener_ptr->active_sockets_no] = listener_socket_fd;

                /* Increase the counter */
                listener_ptr->active_sockets_no++;

                /* Set return value to success */
                res = STATUS_SUCCESS;
            }

            /* TODO: check separately each listener, not in batch. */

            server_info_out = server_info_out->ai_next;
        }
    }

    /* Freeup the memory allocated in server_info out */
    freeaddrinfo(server_info_out);

    return res;
}

static int init_epoll_instance(listener_t *listener_ptr)
{
    /* Result value */
    int res = STATUS_FAILURE;

    if(listener_ptr != NULL)
    {
        /* Create epoll instance */
        int epoll_fd = epoll_new();

        /* Check epoll instance */
        if(epoll_fd == -1)
        {
            log_error("[listener] init_epoll_instance failed creating epoll", strerror(errno));
        }

        else
        {
            /* Set listener's epoll instance */
            listener_ptr->epoll_fd = epoll_fd;

            /* Register the read end of the pipeline control pipe */
            if(epoll_add_in(epoll_fd, listener_ptr->pipeline->pipe_fds[0]) == -1)
            {
                log_error("[listener] init_epoll_instance failed to add pipe read end to epoll",
                          strerror(errno));
            }

            /* Register all listening sockets with epoll */
            else if(register_listener_sockets_to_epoll(listener_ptr) != STATUS_SUCCESS)
            {
                log_error(
                    "[listener] init_epoll_instance register_listener_sockets_to_epoll failed",
                    strerror(errno));
            }

            /* Everything went ok */
            else
            {
                res = STATUS_SUCCESS;
            }
        }
    }
    else
    {
        log_error("[listener] init_epoll_instance invalid input", strerror(errno));
    }

    return res;
}

static int register_listener_sockets_to_epoll(listener_t *listener_ptr)
{
    /* Result value */
    int res = STATUS_FAILURE;

    for(uint i = 0; i < listener_ptr->active_sockets_no; ++i)
    {
        if(listener_ptr->sockets_fds[i] >= 0)
        {
            if(epoll_add_in(listener_ptr->epoll_fd, listener_ptr->sockets_fds[i]) != -1)
            {
                res = STATUS_SUCCESS;
            }

            else
            {
                log_error("[listener] init_epoll_instance failed to add listener fd to epoll",
                          strerror(errno));
                res = STATUS_FAILURE;
                break;
            }
        }
        else
        {
            log_error("[listener] init_epoll_instance socket OFF", strerror(errno));
            res = STATUS_FAILURE;
            break;
        }
    }

    return res;
}

static int read_worker_message(listener_t *listener_ptr)
{
    /* Result variable */
    worker_status res = WORKER_STATUS_INVALID;

    /* Read the pipe to clear it */
    uint32_t worker_msg;

    if(read(listener_ptr->pipeline->pipe_fds[0], &worker_msg, sizeof(uint32_t)) != sizeof(uint32_t))
    {
        log_error("[listener] read_worker_message Failed to read from pipe: %s", strerror(errno));
    }

    else
    {
#ifdef DEBUG_MODE
        log_info("[listener] read_worker_message Pipe event received, worker_msg %ld, size %ld",
                 worker_msg, sizeof(uint32_t));
#endif /* DEBUG_MODE */

        res = (worker_status)worker_msg;
    }

    return res;
}

static int on_worker_status_change(listener_t *listener_ptr, worker_status worker_actual_status)
{
    /* return value */
    int res = STATUS_FAILURE;

    switch(worker_actual_status)
    {
        case WORKER_STATUS_ACTIVE:
            log_info("[listener] on worket status change, WORKER_STATUS_ACTIVE");
            // resume_listening(listener_ptr);
            init_listening_sockets(listener_ptr, listener_ptr->port);
            register_listener_sockets_to_epoll(listener_ptr);

            res = STATUS_SUCCESS;
            break;

        case WORKER_STATUS_FULL:

            log_info("[listener] on worket status change, WORKER_STATUS_FULL");
            // pause_listening(listener_ptr);
            stop_listener(listener_ptr);

            res = STATUS_SUCCESS;
            break;

        default:
            log_error("[listener] Unknown worker_status: %s", strerror(errno));
            break;
    }

    return res;
}

#if 0
static int pause_listening(listener_t *listener_ptr)
{
    /* Result value */
    int res = STATUS_FAILURE;

    for(int i = 0; i < listener_ptr->active_sockets_no; ++i)
    {
        int fd = listener_ptr->sockets_fds[i];
        if(epoll_ctl(listener_ptr->epoll_fd, EPOLL_CTL_DEL, fd, NULL) != 0)
        {
            log_error("[listener] pause_listening: error re-adding fd %d, %s", fd, strerror(errno));
        }

        else
        {
            res = STATUS_SUCCESS;
        }
    }
#    ifdef DEBUG_MODE
    log_info("[listener] pause_listening: listening paused");
#    endif /* DEBUG_MODE */

    return res;
}

static int resume_listening(listener_t *listener_ptr)
{
    /* Result value */
    int res = STATUS_FAILURE;

    struct epoll_event ev = {.events = EPOLLIN};
    for(int i = 0; i < listener_ptr->active_sockets_no; ++i)
    {
        ev.data.fd = listener_ptr->sockets_fds[i];
        if(epoll_ctl(listener_ptr->epoll_fd, EPOLL_CTL_ADD, listener_ptr->sockets_fds[i], &ev) != 0)
        {
            log_error("[listener] resume_listening: error re-adding fd %d, %s", ev.data.fd,
                      strerror(errno));
        }

        else
        {
            res = STATUS_SUCCESS;
        }
    }

#    ifdef DEBUG_MODE
    log_info("[listener] resume_listening: listening resumed");
#    endif /* DEBUG_MODE */

    return res;
}

#endif

static int stop_listener(listener_t *listener_ptr)
{
    /* Result value */
    int res = STATUS_FAILURE;

#ifdef DEBUG_MODE
    log_info("[listener]: stop_listener: un-epolling and closing all listening sockets");
#endif /* DEBUG_MODE */
    if(listener_ptr == NULL || listener_ptr->epoll_fd < 0)
    {
        log_error("[listener]: stop_listener: invalid listener pointer");
    }

    /* loop through all listener's sockets */
    else
    {
        for(uint i = 0; i < listener_ptr->active_sockets_no; i++)
        {
            int fd = listener_ptr->sockets_fds[i];

            /* check if listener is active */
            if(fd > 0)
            {
                struct linger lngr = {.l_onoff = 1, .l_linger = 0};

                /* Unregister each listen‑fd from epoll */
                if(epoll_del(listener_ptr->epoll_fd, fd) != 0)
                {
                    log_error("[listener]: epoll_ctl DEL failed on fd %d: %s", fd, strerror(errno));
                }

                /* disable the socket to force RST on any in‑flight handshakes (SO_LINGER with zero
                 * timeout makes close() send RST instead of FIN) */
                else if(setsockopt(fd, SOL_SOCKET, SO_LINGER, &lngr, sizeof(lngr)) != 0)
                {
                    log_error("[listener]: setsockopt SO_LINGER failed on fd %d: %s", fd,
                              strerror(errno));
                }

                /* close listener file descriptor */
                else if(close(fd) != 0)
                {
                    log_error("[listener]: close failed on fd %d: %s", fd, strerror(errno));
                }

                /* Everything went good */
                else
                {
                    /* reset to -1 */
                    listener_ptr->sockets_fds[i] = -1;
                    res = STATUS_SUCCESS;
                }
            }
            else
            {
                continue;
            }
        }

        /* reset listener sockets number */
        listener_ptr->active_sockets_no = 0;
    }

    return res;
}

static void listener_shutdown(listener_t *listener_ptr)
{
#ifdef DEBUG_MODE
    log_info("[listener]: shutting down...");
#endif /* DEBUG_MODE */

    /* Close listener's listener sockets */
    stop_listener(listener_ptr);

    /* Close listener's epoll instance */
    (void)epoll_shutdown(listener_ptr->epoll_fd);

    /* Free the ptr */
    free(listener_ptr);
}

#ifdef DEBUG_MODE
static int listener_accept_and_p_client_info(int listen_fd)
{
    struct sockaddr_storage client_addr;
    socklen_t addrlen = sizeof(client_addr);

    /* Accept a new client connection */
    int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addrlen);

    char ipstr[INET6_ADDRSTRLEN];
    if(client_addr.ss_family == AF_INET)
    {
        struct sockaddr_in *s = (struct sockaddr_in *)&client_addr;
        inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof(ipstr));
        log_info("[listener] Accepted connection from %s:%d", ipstr, ntohs(s->sin_port));
    }
    else if(client_addr.ss_family == AF_INET6)
    {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&client_addr;
        inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof(ipstr));
        log_info("[listener] Accepted connection from [%s]:%d", ipstr, ntohs(s->sin6_port));
    }

    return client_fd;
}
#endif /* DEBUG_MODE */
