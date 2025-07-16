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
#include <sys/epoll.h>   /* epoll_create1(), epoll_ctl(), epoll_wait(), struct epoll_event */
#include <unistd.h>      /* fork(), close(), read(), write(), etc. */

#include "logger.h"          /* logger */
#include "server_settings.h" /* settings */
#include "socket_helper.h"   /* socket helper */

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
    /* Listening sockets file descriptors */
    int sockets_fds[MAX_LISTENERS];

    /* Total active listening sockets */
    int active_sockets_no;

    /* Listener status variable */
    atomic_listener_status_t status;

    /* collaboration with worker structure */
    pipeline_t *pipeline;
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
static int create_listener(listener_t **listener_ptr, const char *port);

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

static void pause_listening(listener_t *L, int epoll_fd);

static void resume_listening(listener_t *L, int epoll_fd);

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
    log_info("listener_init");
    /* return value */
    int res = STATUS_FAILURE;

    /* Check input */
    if(port == NULL || pipeline_ptr == NULL)
    {
        log_error("listener_init invalid input");
    }

    /* Initialize memory */
    else if(init_memory(listener_ptr) != STATUS_SUCCESS)
    {
        log_error("listener memory initialization failed ", strerror(errno));
    }

    /* Create and start the listener sockets */
    else if(create_listener(listener_ptr, port) != STATUS_SUCCESS)
    {
        log_error("listener start failed ", strerror(errno));
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

    /* main listener thread loop */
    else
    {
        listener_t *listener_ptr = (listener_t *)arg;

        /* Create epoll instance */
        int epoll_fd = epoll_create1(0);

        /** Create epoll event
         * The event argument describes the object linked to the file descriptor fd.  The struct
         * epoll_event is described in epoll_event(3type). The data member of the epoll_event
         * structure specifies data that the kernel should save and then return (via epoll_wait(2))
         * when this file descriptor becomes ready. The events member of the epoll_event structure
         * is a bit mask composed by ORing together zero or more event types, returned by
         * epoll_wait(2), and input flags, which affect its behaviour, but aren't returned.
         */
        struct epoll_event ev;

        /** Set the epoll event to monitor for incoming connections
         * EPOLLIN: The associated file is available for read(2) operations
         * EPOLLOUT: The associated file is available for write(2) operations
         * EPOLLRDHUP: (since Linux 2.6.17) Stream  socket  peer  closed  connection, or shut down
         * writing half of connection.
         * EPOLLERR: Error condition happened on the associated file descriptor.  This event is also
         * reported for the write end of a pipe when the read end has been closed.
         * EPOLLONESHOT: (since Linux 2.6.2) Requests one-shot notification for the associated file
         * descriptor.  This means that after an event notified for the file descriptor by
         * epoll_wait(2), the file descriptor is disabled in the interest list and no other events
         * will be reported by the epoll interface.  The user must call epoll_ctl() with
         * EPOLL_CTL_MOD to rearm the file descriptor with a new event mask.
         */
        ev.events = EPOLLIN;

        /* Register all listening sockets with epoll */
        for(int i = 0; i < listener_ptr->active_sockets_no; ++i)
        {
            /**
             * int epoll_ctl(int epfd, int op, int fd, struct epoll_event *_Nullable event);
             * This  system  call  is used to add, modify, or remove entries in the interest list of
             * the epoll(7) instance referred to by the file descriptor epfd.  It requests that the
             * operation op be performed for the target file descriptor, fd.
             */
            ev.data.fd = listener_ptr->sockets_fds[i];
            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener_ptr->sockets_fds[i], &ev);
        }

        /* Register the read end of the pipeline control pipe */
        int pipe_read_fd = listener_ptr->pipeline->pipe_fds[0];
        ev.data.fd = pipe_read_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pipe_read_fd, &ev);
        /* At this point the pipe is added to epoll ctl before entering infinite loop */

        worker_status worker_old_status = (worker_status)LISTENER_STATUS_ACTIVE;
        worker_status worker_actual_status = (worker_status)LISTENER_STATUS_ACTIVE;

        /* Create epoll events */
        struct epoll_event events[MAX_FAN_OUT_SOCKETS];

        while(atomic_load(&listener_ptr->status) == LISTENER_STATUS_ACTIVE)
        {
            /* Wait for epoll events on pipe / listener sockets */
            int nfds = epoll_wait(epoll_fd, events, MAX_LISTENERS, -1);

            if(nfds < 0)
            {
                log_error("listener: epoll_wait failed: %s", strerror(errno));
                continue;
            }

            for(int i = 0; i < nfds; ++i)
            {
                int listen_fd = events[i].data.fd;

                /* Check if the worker on pipe has something to say */
                if(listen_fd == pipe_read_fd)
                {
#ifdef DEBUG_MODE
                    log_info("[listener] pipe_read_fd %d", listen_fd);
#endif
                    /* Read the pipe to clear it */
                    uint32_t worker_msg;
                    if(read(pipe_read_fd, &worker_msg, sizeof(uint32_t)) != sizeof(uint32_t))
                    {
                        log_error("[listener] Failed to read from pipe: %s", strerror(errno));
                        continue;
                    }

                    /* Set worker status just received on pipe */
                    else
                    {
                        worker_actual_status = (worker_status)worker_msg;
#ifdef DEBUG_MODE
                        log_info(
                            "[listener] Pipe event received on fd %d, worker_msg %ld, size %ld",
                            pipe_read_fd, worker_msg, sizeof(uint32_t));
#endif /* DEBUG_MODE */
                        /* Check the worker's status and set the listener accordingly */
                        /* Check if a status change occurred */
                        if(worker_actual_status != worker_old_status)
                        {
#ifdef DEBUG_MODE
                            log_info("[listener] worker_actual_status[%d] != worker_old_status[%d]",
                                     worker_actual_status, worker_old_status);
#endif /* DEBUG_MODE */
                            switch(worker_actual_status)
                            {
                                case WORKER_STATUS_ACTIVE:

                                    resume_listening(listener_ptr, epoll_fd);
                                    break;

                                case WORKER_STATUS_FULL:

                                    pause_listening(listener_ptr, epoll_fd);
                                    break;

                                default:
                                    log_error("[listener] Unknown worker_status: %s",
                                              strerror(errno));
                                    break;
                            }

                            /* Update the old status */
                            worker_old_status = worker_actual_status;
                        }
                    }
                    continue;
                }

                /* Not a pipe so a listener catched a new connecting client */
                else
                {
                    switch(worker_actual_status)
                    {
                        case WORKER_STATUS_ACTIVE:

#ifdef DEBUG_MODE
                            log_info(
                                "[listener] a new client incoming, worker status active, "
                                "accepting... ");
#endif /* DEBUG_MODE */

                            /** Accept a new client connection
                             * At this point, the TCP connection is established — the kernel queues
                             * the connection into the pending accept queue
                             */
#ifdef DEBUG_MODE
                            int client_fd = listener_accept_and_p_client_info(listen_fd);
#else
                            /**
                             * Pulls the connection from the completed handshake queue.
                             * Returns a new file descriptor (client_fd) for that client connection.
                             * No new packet is sent at that exact moment — the handshake is already
                             * done. the application is now ready to read/write on client_fd.
                             */
                            int client_fd = accept(listen_fd, NULL, NULL);
#endif /* DEBUG_MODE */

                            if(client_fd >= 0)
                            {
                                /* Set socket settings */
                                client_socket_init(&client_fd);

                                /* Check if ring has free space */
                                if(!spsc_ring_is_full(listener_ptr->pipeline->ring_ptr))
                                {
                                    /* Check if push on ring successful */
                                    if(spsc_ring_push(listener_ptr->pipeline->ring_ptr,
                                                      client_fd) != 0)
                                    {
                                        close(client_fd);
                                        log_error("[listener]: spsc_ring_push failed! Fd %d closed",
                                                  client_fd);
                                    }

                                    /* Send a wake-up signal */
                                    else
                                    {
                                        uint64_t inc = 1;
                                        write(listener_ptr->pipeline->wakeup_fd, &inc,
                                              sizeof(uint64_t));
#ifdef DEBUG_MODE
                                        log_info("[listener] wakeup signal sent to worker");
#endif
                                    }
                                }
                                else
                                {
                                    log_error(
                                        "[listener] spsc_ring_is_full, fd %d refused and closed",
                                        client_fd);
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
        listener_shutdown(&listener_ptr);
        close(epoll_fd);
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
        /* Log the status change */
        log_info("Listener status set to %d", status);
#endif /* DEBUG_MODE */
    }
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int create_listener(listener_t **listener_ptr, const char *port)
{
    /* return value */
    int res = STATUS_FAILURE;

    /* Hints for getaddrinfo research */
    struct addrinfo hints;

    /* getaddrinfo output info */
    struct addrinfo *server_info_out = NULL;

    /* Set socket hints for getaddr */
    if(set_socket_hints(&hints) != STATUS_SUCCESS)
    {
        log_error("listener socket hints failed ", strerror(errno));
    }

    /* get connectable sockets with hints */
    else if(getaddrinfo(NULL, port, &hints, &server_info_out) != 0)
    {
        log_error("listener getaddrinfo failed ", strerror(errno));
    }

    else
    {
        log_info("getaddrinfo(NULL, %s) succeeded. Printing address info list results:\n", port);
        log_addrinfo_list(server_info_out);

        /* loop through all the getaddrinfo output results */
        while(server_info_out != NULL)
        {
            /* Create a socket */
            int listener_socket_fd =
                socket(server_info_out->ai_family, server_info_out->ai_socktype,
                       server_info_out->ai_protocol);

            if(listener_socket_fd == -1)
            {
                log_error("socket creation failed: %s\n", strerror(errno));
            }

            /* check if available space for more sockets */
            else if((*listener_ptr)->active_sockets_no >= MAX_LISTENERS)
            {
                log_error("Maximum number of listeners reached: %d", MAX_LISTENERS);
                /* delete the socket */
                close(listener_socket_fd);
            }

            /* Set socket options */
            else if(listener_socket_init(&listener_socket_fd, &server_info_out->ai_family) !=
                    STATUS_SUCCESS)
            {
                log_error("setsockopt failed: %s\n", strerror(errno));
                /* delete the socket */
                close(listener_socket_fd);
            }

            /* Bind the socket */
            else if(bind(listener_socket_fd, server_info_out->ai_addr,
                         server_info_out->ai_addrlen) == -1)
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

            /* If everything worked fine */
            else
            {
                /* Put the socket in the listener */
                (*listener_ptr)->sockets_fds[(*listener_ptr)->active_sockets_no] =
                    listener_socket_fd;

                /* Increase the counter */
                (*listener_ptr)->active_sockets_no++;

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

static void pause_listening(listener_t *L, int epoll_fd)
{
    for(int i = 0; i < L->active_sockets_no; ++i)
    {
        int fd = L->sockets_fds[i];
        if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) != 0)
        {
            log_error("[listener] pause_listening: error re-adding fd %d, %s", fd, strerror(errno));
        }
    }
#ifdef DEBUG_MODE
    log_info("[listener] pause_listening: listening paused");
#endif /* DEBUG_MODE */
}

static void resume_listening(listener_t *L, int epoll_fd)
{
    struct epoll_event ev = {.events = EPOLLIN};
    for(int i = 0; i < L->active_sockets_no; ++i)
    {
        ev.data.fd = L->sockets_fds[i];
        if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, L->sockets_fds[i], &ev) != 0)
        {
            log_error("[listener] resume_listening: error re-adding fd %d, %s", ev.data.fd,
                      strerror(errno));
        }
    }

#ifdef DEBUG_MODE
    log_info("[listener] resume_listening: listening resumed");
#endif /* DEBUG_MODE */
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

static void listener_shutdown(listener_t **listener_ptr)
{
#ifdef DEBUG_MODE
    log_info("[listener]: shutting down...");
#endif /* DEBUG_MODE */
    listener_close(listener_ptr);
    free(*listener_ptr);
}

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
