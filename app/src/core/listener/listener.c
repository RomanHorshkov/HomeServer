/**
 * @file listener.c
 * @brief TCP listener implementation for accepting incoming client connections.
 *
 * This module manages the server's listening sockets across multiple address families (IPv4/IPv6). It uses a reactor pattern to monitor
 * accept events and forwards new client connections to the worker pipeline for processing.
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025
 */

#define _GNU_SOURCE

#include <db_server/core/listener/listener.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <db_server/core/config_core.h>
#include <emlog.h>
#include <db_server/core/reactor.h>
#include <db_server/utils/affinity.h>
#include <db_server/utils/socket_helper.h>
#include <db_server/core/worker/worker.h>

/*****************************************************************************************************************************************
 * PRIVATE DEFINES
 *****************************************************************************************************************************************
 */

#define LOG_TAG "srv_listener"

/*****************************************************************************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 *****************************************************************************************************************************************
 */

/**
 * @brief Internal listener state structure.
 *
 * Manages the lifecycle of listening sockets, reactor instance, and pipeline integration for accepting and forwarding client connections.
 */
typedef struct
{
    listener_status_t status;                                         /**< Current listener status */
    reactor_t         reactor;                                        /**< Reactor for monitoring accept events */
    char              spec[108];                                      /**< Listen spec: a TCP port ("3490") OR a unix path ("/run/…") */
    char              unix_path[108];                                 /**< The bound AF_UNIX path (to unlink on stop), "" if TCP */
    int               sockets_fds[SERVER_CORE_MAX_LISTENING_SOCKETS]; /**< Array of listening socket FDs */
    uint32_t          active_sockets_no;                              /**< Number of active listening sockets */
    // pipeline_t *pipeline;                                        /**< Pointer to worker pipeline */
} listener_t;

static listener_t _listener = {0};

/*****************************************************************************************************************************************
 * PRIVATE FUNCTIONS DECLARATIONS
 *****************************************************************************************************************************************
 */

/**
 * @brief Initialize listening sockets for the given port.
 *
 * Creates and binds TCP listening sockets for all available address families (IPv4 and IPv6), configures socket options, and stores file
 * descriptors in the listener state.
 *
 * @param[in] port Port number to bind to (as string).
 * @retval STATUS_SUCCESS At least one listening socket was successfully created.
 * @retval STATUS_FAILURE No listening sockets could be initialized.
 */
static int _init_listening_sockets(const char* port);

/**
 * @brief Register all listening sockets with the reactor.
 *
 * Allocates context structures for each listening socket and registers them with the reactor for monitoring EPOLLIN events (incoming
 * connections).
 *
 * @retval STATUS_SUCCESS All listening sockets registered successfully.
 * @retval STATUS_FAILURE Failed to register one or more sockets.
 */
static int _register_listening_sockets(void);

/**
 * @brief Handle accept events on a listening socket.
 *
 * Called by the reactor when a new connection is available. Accepts the client connection, initializes the client socket, and pushes it to
 * the worker pipeline.
 *
 * @param[in] fd       File descriptor of the listening socket.
 * @param[in] ctx      Context associated with the listening socket.
 * @retval STATUS_SUCCESS Client connection accepted and queued successfully.
 * @retval STATUS_FAILURE Failed to accept or queue the connection.
 */
static int _handle_listen_event(int fd, fd_ctx_t* ctx);

/**
 * @brief Stop the listener and close all listening sockets.
 *
 * Closes all active listening socket file descriptors and resets the listener state.
 *
 * @param[in,out] l Pointer to the listener structure.
 */
static void _stop_listener(listener_t* l);

/*****************************************************************************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

int listener_init(const char* port /*, void *pipeline_ptr*/)
{
    if(!port || port[0] == '\0' /*|| pipeline_ptr == NULL*/)
    {
        EML_ERROR(LOG_TAG, "listener_init: invalid input");
        return STATUS_FAILURE;
    }

    // _listener.pipeline = (pipeline_t*)pipeline_ptr;
    strncpy(_listener.spec, port, sizeof(_listener.spec) - 1);

    /* Init listener's reactor */
    if(reactor_init(&_listener.reactor) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "listener_init: reactor_init failed");
        return STATUS_FAILURE;
    }

    /* Initialize listener's listening sockets */
    if(_init_listening_sockets(_listener.spec) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "listener_init: socket init failed");
        reactor_shutdown(&_listener.reactor);
        return STATUS_FAILURE;
    }

    /* register listening sockets to reactor */
    if(_register_listening_sockets() != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "listener_init: register sockets failed");
        _stop_listener(&_listener);
        reactor_shutdown(&_listener.reactor);
        return STATUS_FAILURE;
    }

    _listener.status = LISTENER_STATUS_ACTIVE;
    return STATUS_SUCCESS;
}

void* listener_run(void* arg)
{
    (void)arg;

    /* The listener/dispatcher owns core 0 (slot 0); operators own 1.. . */
    srv_affinity_pin_self("listener", 0);

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

/*****************************************************************************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

/**
 * @brief Bind an AF_UNIX stream listener at @p path — the production nginx↔backend transport.
 *
 * A unix socket removes the loopback TCP port entirely (no backend port to bind, expose, or scan). The socket is created 0660 so nginx
 * (www-data, a member of the home_server group) can connect, while nothing off-box ever can. A stale socket from a previous run is
 * unlinked first. The accepted fds flow through the exact same byte pipeline as TCP — accept() never reads the peer address.
 */
static int _init_unix_socket(const char* path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd == -1)
    {
        EML_PERR(LOG_TAG, "listener: AF_UNIX socket() failed");
        return STATUS_FAILURE;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if(strlen(path) >= sizeof addr.sun_path)
    {
        EML_ERROR(LOG_TAG, "listener: unix socket path too long (%s)", path);
        close(fd);
        return STATUS_FAILURE;
    }
    strncpy(addr.sun_path, path, sizeof addr.sun_path - 1);

    (void)unlink(path); /* a stale socket from a prior run is not an error */

    if(bind(fd, (const struct sockaddr*)&addr, (socklen_t)sizeof addr) == -1)
    {
        EML_PERR(LOG_TAG, "listener: bind(%s) failed", path);
        close(fd);
        return STATUS_FAILURE;
    }
    /* 0660: owner (home_server) + group (home_server, which nginx's www-data joins) — never world. */
    if(chmod(path, 0660) == -1)
    {
        EML_PERR(LOG_TAG, "listener: chmod(%s, 0660) failed", path);
        close(fd);
        (void)unlink(path);
        return STATUS_FAILURE;
    }
    if(listen(fd, SERVER_CORE_MAX_PENDING_SOCKETS_PER_LISTENER) == -1)
    {
        EML_PERR(LOG_TAG, "listener: listen(%s) failed", path);
        close(fd);
        (void)unlink(path);
        return STATUS_FAILURE;
    }
    if(socket_set_non_blocking(&fd) != STATUS_SUCCESS)
    {
        close(fd);
        (void)unlink(path);
        return STATUS_FAILURE;
    }

    _listener.sockets_fds[_listener.active_sockets_no++] = fd;
    strncpy(_listener.unix_path, path, sizeof(_listener.unix_path) - 1);
    EML_INFO(LOG_TAG, "listener: bound AF_UNIX %s (0660, no TCP port)", path);
    return STATUS_SUCCESS;
}

static int _init_listening_sockets(const char* port)
{
    struct addrinfo  hints;
    struct addrinfo* ai = NULL;

    /* A spec that begins with '/' is a unix socket PATH (the production transport);
     * anything else is a TCP port (dev, tests, direct-LAN trials). */
    if(port[0] == '/')
    {
        return _init_unix_socket(port);
    }

    if(socket_listener_set_hints(&hints) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "listener: hints setup failed");
        return STATUS_FAILURE;
    }

    /* Security default: bind LOOPBACK ONLY. In production nginx faces the
     * network and proxies to us on 127.0.0.1 (SECURITY.md §1 — the C backend
     * is never directly exposed). DB_SERVER_BIND overrides the host for
     * deliberate direct access (e.g. "0.0.0.0" for a LAN dev trial without
     * nginx); doing so is an explicit, logged choice, never the default. */
    const char* bind_host = getenv("DB_SERVER_BIND");
    if(!bind_host || bind_host[0] == '\0')
    {
        bind_host = "127.0.0.1";
    }
    if(strcmp(bind_host, "127.0.0.1") != 0 && strcmp(bind_host, "::1") != 0 && strcmp(bind_host, "localhost") != 0)
    {
        EML_WARN(LOG_TAG, "listener: binding NON-LOOPBACK host '%s' — the backend is directly network-exposed (nginx should front it)",
                 bind_host);
    }
    EML_INFO(LOG_TAG, "listener: binding %s:%s", bind_host, port);

    const int gai_rc = getaddrinfo(bind_host, port, &hints, &ai);
    if(gai_rc != 0)
    {
        EML_ERROR(LOG_TAG, "listener: getaddrinfo(%s:%s) failed: %s", bind_host, port, gai_strerror(gai_rc));
        return STATUS_FAILURE;
    }

    for(const struct addrinfo* cur = ai; cur != NULL; cur = cur->ai_next)
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
            EML_ERROR(LOG_TAG, "listener: socket_listener_init failed");
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
        char        ip_str[INET6_ADDRSTRLEN];
        void*       addr  = NULL;
        const char* ipver = "";
        if(cur->ai_family == AF_INET)
        {
            struct sockaddr_in* ipv4 = (struct sockaddr_in*)cur->ai_addr;
            addr                     = &(ipv4->sin_addr);
            ipver                    = "IPv4";
        }
        else if(cur->ai_family == AF_INET6)
        {
            struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)cur->ai_addr;
            addr                      = &(ipv6->sin6_addr);
            ipver                     = "IPv6";
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
        int       fd  = _listener.sockets_fds[i];
        fd_ctx_t* ctx = calloc(1, sizeof(*ctx));
        if(!ctx)
        {
            EML_ERROR(LOG_TAG, "listener: context allocation failed");
            return STATUS_FAILURE;
        }
        ctx->fd      = fd;
        ctx->owner   = &_listener;
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

static int _handle_listen_event(int fd, fd_ctx_t* ctx)
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
        EML_ERROR(LOG_TAG, "worker_dispatch_to_operator failed");
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

static void _stop_listener(listener_t* l)
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

    /* Remove the AF_UNIX socket file so a restart binds cleanly (bind also
     * unlinks a stale one, but leaving it around is untidy). */
    if(l->unix_path[0] != '\0')
    {
        (void)unlink(l->unix_path);
        l->unix_path[0] = '\0';
    }
}
