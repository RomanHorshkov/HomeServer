/**
 * @file listener.c
 * @brief TCP listener implementation for accepting incoming client connections.
 *
 * This module manages the server's listening sockets across multiple address families
 * (IPv4/IPv6). It uses a reactor pattern to monitor accept events and forwards new
 * client connections to the worker pipeline for processing.
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025
 */

#define _GNU_SOURCE

#include "listener/listener.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emlog.h"
#include "config_core.h"
#include "reactor.h"
#include "socket_helper.h"
#include "worker.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "srv_listener"

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */

/**
 * @brief Internal listener state structure.
 *
 * Manages the lifecycle of listening sockets, reactor instance, and pipeline
 * integration for accepting and forwarding client connections.
 */
typedef struct
{
    listener_status_t status;                                    /**< Current listener status */
    reactor_t reactor;                                           /**< Reactor for monitoring accept events */
    char port[6];                                                /**< Port number as string */
    int sockets_fds[SERVER_CORE_MAX_LISTENING_SOCKETS];          /**< Array of listening socket FDs */
    uint32_t active_sockets_no;                                  /**< Number of active listening sockets */
    // pipeline_t *pipeline;                                        /**< Pointer to worker pipeline */
} listener_t;

static listener_t _listener = {0};

/****************************************************************************
 * PRIVATE FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief Initialize listening sockets for the given port.
 *
 * Creates and binds TCP listening sockets for all available address families
 * (IPv4 and IPv6), configures socket options, and stores file descriptors in
 * the listener state.
 *
 * @param[in] port Port number to bind to (as string).
 * @retval STATUS_SUCCESS At least one listening socket was successfully created.
 * @retval STATUS_FAILURE No listening sockets could be initialized.
 */
static int _init_listening_sockets(const char *port);

/**
 * @brief Register all listening sockets with the reactor.
 *
 * Allocates context structures for each listening socket and registers them
 * with the reactor for monitoring EPOLLIN events (incoming connections).
 *
 * @retval STATUS_SUCCESS All listening sockets registered successfully.
 * @retval STATUS_FAILURE Failed to register one or more sockets.
 */
static int _register_listening_sockets(void);

/**
 * @brief Handle accept events on a listening socket.
 *
 * Called by the reactor when a new connection is available. Accepts the client
 * connection, initializes the client socket, and pushes it to the worker pipeline.
 *
 * @param[in] fd       File descriptor of the listening socket.
 * @param[in] ctx      Context associated with the listening socket.
 * @retval STATUS_SUCCESS Client connection accepted and queued successfully.
 * @retval STATUS_FAILURE Failed to accept or queue the connection.
 */
static int _handle_listen_event(int fd, fd_ctx_t *ctx);

/**
 * @brief Stop the listener and close all listening sockets.
 *
 * Closes all active listening socket file descriptors and resets the listener state.
 *
 * @param[in,out] l Pointer to the listener structure.
 */
static void _stop_listener(listener_t *l);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int listener_init(const char *port/*, void *pipeline_ptr*/)
{
    if(!port || port[0] == '\0' /*|| pipeline_ptr == NULL*/)
    {
        EML_ERROR(LOG_TAG, "listener_init: invalid input");
        return STATUS_FAILURE;
    }

    // _listener.pipeline = (pipeline_t*)pipeline_ptr;
    strncpy(_listener.port, port, sizeof(_listener.port) - 1);

    /* Init listener's reactor */
    if(reactor_init(&_listener.reactor) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "listener_init: reactor_init failed");
        return STATUS_FAILURE;
    }

    /* Initialize listener's listening sockets */
    if(_init_listening_sockets(_listener.port) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "listener_init: socket init failed");
        reactor_shutdown(&_listener.reactor);
        return STATUS_FAILURE;
    }

    /* register listening sockets to reactor */
    if(_register_listening_sockets() != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "listener_init: register sockets failed");
        _stop_listener(&_listener);
        reactor_shutdown(&_listener.reactor);
        return STATUS_FAILURE;
    }

    _listener.status = LISTENER_STATUS_ACTIVE;
    return STATUS_SUCCESS;
}

void *listener_run(void *arg)
{
    (void)arg;

    while(_listener.status == LISTENER_STATUS_ACTIVE)
    {
        if(reactor_run(&_listener.reactor, NULL) != STATUS_SUCCESS)
        {
            EML_ERROR(LOG_TAG, "reactor_run failed");
        }
    }

    _stop_listener(&_listener);
    reactor_shutdown(&_listener.reactor);
    return NULL;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int _init_listening_sockets(const char *port)
{
    struct addrinfo hints;
    struct addrinfo *ai = NULL;

    if(socket_listener_set_hints(&hints) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "listener: hints failed");
        return STATUS_FAILURE;
    }

    if(getaddrinfo(NULL, port, &hints, &ai) != 0)
    {
        EML_PERR(LOG_TAG, "listener: getaddrinfo failed");
        return STATUS_FAILURE;
    }

    for(const struct addrinfo *cur = ai; cur != NULL; cur = cur->ai_next)
    {
        if(_listener.active_sockets_no >= SERVER_CORE_MAX_LISTENING_SOCKETS)
        {
            EML_WARN(LOG_TAG, "listener: max listening sockets reached (%d)", SERVER_CORE_MAX_LISTENING_SOCKETS);
            break;
        }

        int fd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        if(fd == -1)
        {
            EML_PERR(LOG_TAG, "listener: socket creation failed");
            continue;
        }

        if(socket_listener_init(&fd, &cur->ai_family) != STATUS_SUCCESS)
        {
            EML_PERR(LOG_TAG, "listener: socket_listener_init failed");
            close(fd);
            continue;
        }

        if(bind(fd, cur->ai_addr, cur->ai_addrlen) == -1)
        {
            EML_PERR(LOG_TAG, "listener: bind failed");
            close(fd);
            continue;
        }

        if(listen(fd, SERVER_CORE_MAX_PENDING_SOCKETS_PER_LISTENER) == -1)
        {
            EML_PERR(LOG_TAG, "listener: listen failed");
            close(fd);
            continue;
        }

        _listener.sockets_fds[_listener.active_sockets_no++] = fd;

#ifdef DEBUG
        char ip_str[INET6_ADDRSTRLEN];
        void *addr = NULL;
        const char *ipver = "";
        if(cur->ai_family == AF_INET)
        {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)cur->ai_addr;
            addr = &(ipv4->sin_addr);
            ipver = "IPv4";
        }
        else if(cur->ai_family == AF_INET6)
        {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)cur->ai_addr;
            addr = &(ipv6->sin6_addr);
            ipver = "IPv6";
        }
        if(addr)
        {
            inet_ntop(cur->ai_family, addr, ip_str, sizeof(ip_str));
            EML_DBG(LOG_TAG, "listening on %s:%s", ip_str, port);
        }
        else
        {
            EML_DBG(LOG_TAG, "listening socket created (%s)", ipver);
        }
#endif /* DEBUG */
    }

    freeaddrinfo(ai);
    return (_listener.active_sockets_no > 0) ? STATUS_SUCCESS : STATUS_FAILURE;
}

static int _register_listening_sockets(void)
{
    for(uint32_t i = 0; i < _listener.active_sockets_no; ++i)
    {
        int fd = _listener.sockets_fds[i];
        fd_ctx_t *ctx = calloc(1, sizeof(*ctx));
        if(!ctx)
        {
            EML_PERR(LOG_TAG, "listener: calloc ctx failed");
            return STATUS_FAILURE;
        }
        ctx->fd = fd;
        ctx->owner = &_listener;
        ctx->handler = _handle_listen_event;

        if(reactor_add_in(&_listener.reactor, fd, ctx) != STATUS_SUCCESS)
        {
            EML_PERR(LOG_TAG, "listener: reactor_add_in failed for fd %d", fd);
            free(ctx);
            return STATUS_FAILURE;
        }
    }

    return STATUS_SUCCESS;
}

static int _handle_listen_event(int fd, fd_ctx_t *ctx)
{
    (void)ctx;
#ifdef DEBUG
    EML_DBG(LOG_TAG, "listen event on fd %d", fd);
#endif /* DEBUG */

    int client_fd = accept(fd, NULL, NULL);
    if(client_fd < 0)
    {
        EML_PERR(LOG_TAG, "accept failed");
        return STATUS_FAILURE;
    }

    if(worker_dispatch_to_operator(client_fd) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "worker_dispatch_to_operator failed");
        goto fail;
    }

    // if(pipeline_push(client_fd) != STATUS_SUCCESS)
    // {
    //     EML_PERR(LOG_TAG, "pipeline_push failed");
    //     socket_shutdown_and_close(client_fd);
    //     return STATUS_FAILURE;
    // }

    return STATUS_SUCCESS;

fail:
    socket_shutdown_and_close(client_fd);
    return STATUS_FAILURE;
}

static void _stop_listener(listener_t *l)
{
    if(!l) return;

    for(uint32_t i = 0; i < l->active_sockets_no; ++i)
    {
        if(l->sockets_fds[i] > 0)
        {
            socket_shutdown_and_close(l->sockets_fds[i]);
            l->sockets_fds[i] = -1;
        }
    }
    l->active_sockets_no = 0;
}
