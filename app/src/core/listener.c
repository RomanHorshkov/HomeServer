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

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#define _GNU_SOURCE   /* for accept4 */
#include "listener.h" /* listener */

#include <arpa/inet.h>   /* inet_ntop(), struct sockaddr_in, struct sockaddr_in6 */
#include <errno.h>       /* errno, EADDRINUSE, etc. */
#include <netdb.h>       /* getaddrinfo(), addrinfo, gai_strerror() */
#include <netinet/tcp.h> /* TCP_NODELAY */
#include <stdatomic.h>   /* atomic_int */
#include <stdlib.h>      /* malloc(), calloc(), free, etc. */
#include <string.h>      /* memset(), strcpy(), strlen(), etc. */
#include <unistd.h>      /* fork(), close(), read(), write(), etc. */

#include "reactor.h"         /* reactor */
#include "server_settings.h" /* settings */
#include "socket_helper.h"
#include <emlog.h>

#define LOG_TAG "listener"

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
typedef _Atomic listener_status listener_status_t;

/* Listener internal state */
struct listener
{
    /* Listener status variable */
    listener_status_t status;

    /* reactor instance */
    reactor_t *reactor_ptr;

    /* Port */
    char port[6];

    /* Listening sockets file descriptors */
    int sockets_fds[MAX_LISTENERS];

    /* Total active listening sockets */
    uint active_sockets_no;

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
 * @brief Initialize the communication pipeline for the listener.
 *
 * Assigns the provided pipeline structure to the listener instance. This links
 * the listener to the inter-thread communication mechanism used to transfer
 * client sockets to the worker(s).
 *
 * @param listener_ptr   Pointer to the listener instance.
 * @param pipeline_ptr   Pointer to the pre-initialized pipeline structure.
 * @retval  0  Success.
 * @retval -1 Failure (invalid input parameters).
 */
static int init_pipeline(listener_t *listener_ptr, pipeline_t *pipeline_ptr);

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

static int register_listening_sockets(listener_t *listener_ptr);

static int on_worker_status_change(listener_t *listener_ptr, worker_status worker_actual_status);

static int handle_worker_event(int fd, fd_ctx_t *ctx);

static int handle_listen_event(int fd, fd_ctx_t *ctx);

#ifdef DEBUG_MODE
static void listener_log_addrinfo_list(const struct addrinfo *ai);
#endif

/**
 * @brief Shutdown the listener and release all associated resources.
 *
 * Closes all sockets and frees the listener structure. Should be called when
 * the listener thread is exiting.
 *
 * @param listener_ptr  Pointer to the listener instance.
 */
static void listener_shutdown(listener_t *listener_ptr);

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
    /* return value */
    int res = STATUS_FAILURE;

    /* Check input */
    if(listener_ptr == NULL || port == NULL || pipeline_ptr == NULL)
    {
        EML_ERROR(LOG_TAG, "[listener] _init: invalid input");
    }

    /* Initialize memory */
    else if(init_memory(listener_ptr) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "[listener] _init: memory initialization failed");
    }

    /* Initialize communication pipeline */
    else if(init_pipeline(*listener_ptr, pipeline_ptr) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "[listener] _init: pipeline initialization failed");
    }

    /* Create and start the listener sockets */
    else if(init_listening_sockets(*listener_ptr, port) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "[listener]: _init init_listening_sockets failed");
    }

    /* Initialize reactor */
    else if(reactor_init(&(*listener_ptr)->reactor_ptr) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "[listener]: _init: reactor_init failed: %s", strerror(errno));
    }

    /* Add pipeline read socket to reactor */
    else
    {
        int pipe_fd = pipeline_get_pipe_end_fd((*listener_ptr)->pipeline, 0);

        fd_ctx_t *ctx = calloc(1, sizeof(fd_ctx_t));
        ctx->fd = pipe_fd;
        ctx->owner = listener_ptr;
        ctx->handler = handle_worker_event;

        if(reactor_add_in((*listener_ptr)->reactor_ptr, pipe_fd, ctx) != STATUS_SUCCESS)
        {
            EML_ERROR(LOG_TAG, "[listener]: _init: reactor_add_in failed pipe read: %s", strerror(errno));
        }

        /* Register all listening sockets to reactor */
        else if(register_listening_sockets(*listener_ptr) != STATUS_SUCCESS)
        {
            EML_PERR(LOG_TAG, "[listener]: _init register_listening_sockets failed");
        }

        /* If everything succceded */
        else
        {
            /* Set the pipeline collaboration structure */
            (*listener_ptr)->pipeline = pipeline_ptr;

            /* Set the listener status to on */
            listener_set_status(*listener_ptr, LISTENER_STATUS_ACTIVE);

            /* Set the worker status to on */
            (*listener_ptr)->status_worker = WORKER_STATUS_ACTIVE;

            res = STATUS_SUCCESS;
        }
    }
    return res;
}

void *listener_run(void *arg)
{
    /* Check input */
    if(arg == NULL)
    {
        EML_ERROR(LOG_TAG, "listener_run invalid input");
    }

    else
    {
        listener_t *listener_ptr = (listener_t *)arg;

        /* main listener thread loop */
        while(atomic_load(&listener_ptr->status) == LISTENER_STATUS_ACTIVE)
        {
            reactor_run(listener_ptr->reactor_ptr, NULL);
        }

        /* Cleanup */
        listener_shutdown(listener_ptr);
    }

#ifdef DEBUG_MODE
    EML_INFO(LOG_TAG, "[listener] Listener thread exiting.");
#endif /* DEBUG_MODE */

    return NULL;
}

void listener_set_status(listener_t *listener_ptr, int status)
{
    /* check input */
    if(listener_ptr == NULL)
    {
        EML_ERROR(LOG_TAG, "listener_set_status: invalid listener pointer");
    }

    else if(status >= LISTENER_STATUS_INVALID)
    {
        EML_ERROR(LOG_TAG, "listener_set_status: invalid status value %d", status);
    }

    /* Set the status */
    else
    {
        atomic_store(&listener_ptr->status, status);

#ifdef DEBUG_MODE
        EML_INFO(LOG_TAG, "[listener] status set to %d", status);
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
        listener_t *new_listener;
        /* allocate memory for the listener */
        new_listener = (listener_t *)calloc(1, sizeof(listener_t));

        /* Check memory allocation */
        if(new_listener != NULL)
        {
            *listener_ptr = new_listener;
            res = STATUS_SUCCESS;
#ifdef DEBUG_MODE
            EML_INFO(LOG_TAG, "[listener] memory setted up correctly");
#endif /* DEBUG_MODE */
        }

        else
        {
            EML_PERR(LOG_TAG, "[listener] memory allocation failed calloc");
        }
    }

    else
    {
        EML_PERR(LOG_TAG, "[listener] allocation failed, invalid input");
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
        EML_PERR(LOG_TAG, "[listener] init_pipeline failed, invalid input");
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
        EML_ERROR(LOG_TAG, "[listener] init_listening_sockets socket hints failed: %s", strerror(errno));
    }

    /* get connectable sockets with hints */
    else if(getaddrinfo(NULL, port, &hints, &server_info_out) != 0)
    {
        EML_ERROR(LOG_TAG, "[listener] init_listening_sockets getaddrinfo failed: %s", strerror(errno));
    }

    else
    {
#ifdef DEBUG_MODE
        EML_INFO(LOG_TAG, 
            "[listener] getaddrinfo(NULL, %s) succeeded. Printing address info "
            "list results:\n",
            port);
        listener_log_addrinfo_list(server_info_out);
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
                EML_ERROR(LOG_TAG, "[listener] socket creation failed: %s\n", strerror(errno));
            }

            /* check if available space for more sockets */
            else if(listener_ptr->active_sockets_no >= MAX_LISTENERS)
            {
                EML_ERROR(LOG_TAG, "[listener] Maximum number of listeners reached: %d", MAX_LISTENERS);
                /* delete the socket */
                close(listener_socket_fd);
            }

            /* Set socket options */
            else if(listener_socket_init(&listener_socket_fd, &server_info_out->ai_family) !=
                    STATUS_SUCCESS)
            {
                EML_ERROR(LOG_TAG, "[listener] setsockopt failed: %s\n", strerror(errno));
                /* delete the socket */
                close(listener_socket_fd);
            }

            /* Bind the socket */
            else if(bind(listener_socket_fd, server_info_out->ai_addr,
                         server_info_out->ai_addrlen) == -1)
            {
                EML_ERROR(LOG_TAG, "[listener] bind failed: %s\n", strerror(errno));
                /* delete the socket */
                close(listener_socket_fd);
            }

            /* Start listening */
            else if(listen(listener_socket_fd, MAX_PENDING_CONNECTIONS) == -1)
            {
                EML_ERROR(LOG_TAG, "[listener] listen failed: %s\n", strerror(errno));
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

static int register_listening_sockets(listener_t *listener_ptr)
{
    /* Result value */
    int res = STATUS_FAILURE;

    for(uint i = 0; i < listener_ptr->active_sockets_no; ++i)
    {
        if(listener_ptr->sockets_fds[i] >= 0)
        {
            int fd = listener_ptr->sockets_fds[i];

            fd_ctx_t *ctx = calloc(1, sizeof(fd_ctx_t));
            ctx->fd = fd;
            ctx->owner = listener_ptr;
            ctx->handler = handle_listen_event;

            if(reactor_add_in(listener_ptr->reactor_ptr, listener_ptr->sockets_fds[i], ctx) !=
               STATUS_SUCCESS)
            {
                EML_ERROR(LOG_TAG, 
                    "[listener] reactor_add_in failed to add listener fd %d to "
                    "reactor, %s",
                    listener_ptr->sockets_fds[i], strerror(errno));
                break;
            }

            else
            {
                res = STATUS_SUCCESS;
            }
        }
        else
        {
            EML_PERR(LOG_TAG, "[listener] init_epoll_instance socket OFF, continue");
        }
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
            EML_INFO(LOG_TAG, "[listener] on worket status change, WORKER_STATUS_ACTIVE");
            // resume_listening(listener_ptr);
            init_listening_sockets(listener_ptr, listener_ptr->port);
            // register_listener_sockets_to_epoll(listener_ptr);

            res = STATUS_SUCCESS;
            break;

        case WORKER_STATUS_FULL:

            EML_INFO(LOG_TAG, "[listener] on worket status change, WORKER_STATUS_FULL");
            // pause_listening(listener_ptr);
            stop_listener(listener_ptr);

            res = STATUS_SUCCESS;
            break;

        default:
            EML_ERROR(LOG_TAG, "[listener] Unknown worker_status: %s", strerror(errno));
            break;
    }

    return res;
}

static int handle_worker_event(int fd, fd_ctx_t *ctx)
{
    /* Result variable */
    worker_status res = WORKER_STATUS_INVALID;

    /* Read the pipe to clear it */
    uint32_t worker_msg;

    /* listener ptr */
    listener_t *listener_ptr = (listener_t *)ctx->owner;

    if(read(fd, &worker_msg, sizeof(uint32_t)) != sizeof(uint32_t))
    {
        EML_ERROR(LOG_TAG, "[listener] handle_worker_event Failed to read from pipe: %s", strerror(errno));
    }

    /* Check if a status change occurred */
    else if(((worker_status)worker_msg != listener_ptr->status_worker) &&
            ((worker_status)worker_msg != WORKER_STATUS_INVALID))
    {
#ifdef DEBUG_MODE
        EML_INFO(LOG_TAG, "[listener] worker_actual_status[%d] != worker_old_status[%d]",
                 (worker_status)worker_msg, listener_ptr->status_worker);
#endif /* DEBUG_MODE */

        /* Check if change notification occurred */
        if(on_worker_status_change(listener_ptr, (worker_status)worker_msg) != STATUS_SUCCESS)
        {
            EML_ERROR(LOG_TAG, 
                "[listener] handle_worker_event status change detected but "
                "action faled");
        }

        /* Update the old status */
        else
        {
            listener_ptr->status_worker = (worker_status)worker_msg;
        }
    }

    else
    {
#ifdef DEBUG_MODE
        EML_INFO(LOG_TAG, 
            "[listener] read_worker_message Pipe event received, worker_msg "
            "%u, size %zu",
            worker_msg, sizeof(uint32_t));
#endif /* DEBUG_MODE */

        res = (worker_status)worker_msg;
    }

    return res;
}

static int handle_listen_event(int fd, fd_ctx_t *ctx)
{
#ifdef DEBUG_MODE
    EML_INFO(LOG_TAG, "[listener] handle_listen_eventfd: %d", fd);
#endif /* DEBUG_MODE */
    /* Result variable */
    int res = STATUS_FAILURE;

    /* New client file descriptor */
    int client_fd = -1;

    /* listener ptr */
    listener_t *listener_ptr = (listener_t *)ctx->owner;

    switch(listener_ptr->status_worker)
    {
        case WORKER_STATUS_ACTIVE:
            /**
             * Pulls the connection from the completed handshake q ueue.
             * Returns a new file descriptor (client_fd) for that client connection.
             * No new packet is sent at that exact moment — the handshake is already
             * done. the application is now ready to read/write on client_fd.
             */
#ifdef DEBUG_MODE
            client_fd = listener_accept_and_p_client_info(fd);
#else
            client_fd = accept(fd, NULL, NULL);
#endif /* DEBUG_MODE */

            if(client_fd >= 0)
            {
                /* Set socket settings */
                client_socket_init(&client_fd);

                /* Push to ring and notify worker */
                if(pipeline_push(listener_ptr->pipeline, client_fd) != STATUS_SUCCESS)
                {
                    close(client_fd);
                    EML_ERROR(LOG_TAG, "[listener] pipeline_push FAILED");
                }
                else
                {
                    res = STATUS_SUCCESS;
                }
            }
            else
            {
                EML_ERROR(LOG_TAG, "[listener] Failed to accept client: %s", strerror(errno));
            }

            break;

        case WORKER_STATUS_FULL:

#ifdef DEBUG_MODE
            EML_INFO(LOG_TAG, "[listener] worker status FULL... ");
#endif /* DEBUG_MODE */

            /* Should proceed here and lock the listeners */
            res = STATUS_SUCCESS;

            break;

        default:

#ifdef DEBUG_MODE
            EML_INFO(LOG_TAG, "[listener] worker status UNKNOWN %d", listener_ptr->status_worker);
#endif /* DEBUG_MODE */
            break;
    }

    return res;
}

#ifdef DEBUG_MODE
static void listener_log_addrinfo_list(const struct addrinfo *ai)
{
    if(ai == NULL)
    {
        EML_INFO(LOG_TAG, "addrinfo list is empty");
        return;
    }

    int  index = 0;
    char ip_str[INET6_ADDRSTRLEN];

    for(; ai != NULL; ai = ai->ai_next, ++index)
    {
        void       *addr   = NULL;
        const char *ipver  = "UNKNOWN";
        int         family = ai->ai_family;

        if(family == AF_INET)
        {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)ai->ai_addr;
            addr                     = &(ipv4->sin_addr);
            ipver                    = "IPv4";
        }
        else if(family == AF_INET6)
        {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)ai->ai_addr;
            addr                      = &(ipv6->sin6_addr);
            ipver                     = "IPv6";
        }
        else
        {
            EML_INFO(LOG_TAG, "addrinfo[%d] Unknown family=%d", index, family);
            continue;
        }

        inet_ntop(family, addr, ip_str, sizeof ip_str);

        const char *protocol_name = "UNKNOWN";
        switch(ai->ai_protocol)
        {
            case IPPROTO_TCP:
                protocol_name = "TCP";
                break;
            case IPPROTO_UDP:
                protocol_name = "UDP";
                break;
            default:
                break;
        }

        EML_INFO(LOG_TAG, "addr_info[%d] %s address=%s socktype=%d protocol=%s flags=0x%x",
                 index, ipver, ip_str, ai->ai_socktype, protocol_name, ai->ai_flags);
    }
}
#endif /* DEBUG_MODE */

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
            EML_ERROR(LOG_TAG, "[listener] pause_listening: error re-adding fd %d, %s", fd, strerror(errno));
        }

        else
        {
            res = STATUS_SUCCESS;
        }
    }
#    ifdef DEBUG_MODE
    EML_INFO(LOG_TAG, "[listener] pause_listening: listening paused");
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
            EML_ERROR(LOG_TAG, "[listener] resume_listening: error re-adding fd %d, %s", ev.data.fd,
                      strerror(errno));
        }

        else
        {
            res = STATUS_SUCCESS;
        }
    }

#    ifdef DEBUG_MODE
    EML_INFO(LOG_TAG, "[listener] resume_listening: listening resumed");
#    endif /* DEBUG_MODE */

    return res;
}

#endif

static int stop_listener(listener_t *listener_ptr)
{
    /* Result value */
    int res = STATUS_FAILURE;

#ifdef DEBUG_MODE
    EML_INFO(LOG_TAG, 
        "[listener]: stop_listener: un-epolling and closing all listening "
        "sockets");
#endif /* DEBUG_MODE */
    if(listener_ptr == NULL || listener_ptr->reactor_ptr != NULL)
    {
        EML_ERROR(LOG_TAG, "[listener]: stop_listener: invalid listener pointer");
    }

    // /* loop through all listener's sockets */
    // else
    // {
    //     for(uint i = 0; i < listener_ptr->active_sockets_no; i++)
    //     {
    //         int fd = listener_ptr->sockets_fds[i];

    //         /* check if listener is active */
    //         if(fd > 0)
    //         {
    //             struct linger lngr = {.l_onoff = 1, .l_linger = 0};

    //             /* Unregister each listen‑fd from epoll */
    //             if(epoll_del(listener_ptr->epoll_fd, fd) != 0)
    //             {
    //                 EML_ERROR(LOG_TAG, "[listener]: epoll_ctl DEL failed on fd %d: %s", fd,
    //                 strerror(errno));
    //             }

    //             /* disable the socket to force RST on any in‑flight handshakes (SO_LINGER with
    //             zero
    //              * timeout makes close() send RST instead of FIN) */
    //             else if(setsockopt(fd, SOL_SOCKET, SO_LINGER, &lngr, sizeof(lngr)) != 0)
    //             {
    //                 EML_ERROR(LOG_TAG, "[listener]: setsockopt SO_LINGER failed on fd %d: %s", fd,
    //                           strerror(errno));
    //             }

    //             /* close listener file descriptor */
    //             else if(close(fd) != 0)
    //             {
    //                 EML_ERROR(LOG_TAG, "[listener]: close failed on fd %d: %s", fd, strerror(errno));
    //             }

    //             /* Everything went good */
    //             else
    //             {
    //                 /* reset to -1 */
    //                 listener_ptr->sockets_fds[i] = -1;
    //                 res = STATUS_SUCCESS;
    //             }
    //         }
    //         else
    //         {
    //             continue;
    //         }
    //     }

    //     /* reset listener sockets number */
    //     listener_ptr->active_sockets_no = 0;
    // }

    return res;
}

static void listener_shutdown(listener_t *listener_ptr)
{
#ifdef DEBUG_MODE
    EML_INFO(LOG_TAG, "[listener]: shutting down...");
#endif /* DEBUG_MODE */

    /* Close listener's listener sockets */
    stop_listener(listener_ptr);

    /* Shutdown and free listener's reactor */
    reactor_shutdown(&listener_ptr->reactor_ptr);
    free(listener_ptr->reactor_ptr);

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
        EML_INFO(LOG_TAG, "[listener] Accepted connection from %s:%d", ipstr, ntohs(s->sin_port));
    }
    else if(client_addr.ss_family == AF_INET6)
    {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&client_addr;
        inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof(ipstr));
        EML_INFO(LOG_TAG, "[listener] Accepted connection from [%s]:%d", ipstr, ntohs(s->sin6_port));
    }

    return client_fd;
}
#endif /* DEBUG_MODE */
