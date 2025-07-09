/**
 * @file socket_helper.c
 * @brief Helper functions for socket configuration and management.
 *
 * Implements wrappers for setting socket options such as non-blocking mode,
 * disabling Nagle's algorithm, and initializing listener/client sockets.
 *
 * @author  Roman Horshkov
 * @date    2025-05-11
 */

#include "socket_helper.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h> /* IPPROTO_TCP */
#include <netinet/tcp.h>
#include <string.h>     /* strerror() */
#include <sys/socket.h> /* fcntl(), setsockopt() */

#include "logger.h"          /* log_error */
#include "server_settings.h" /* STATUS_SUCCESS, STATUS_FAILURE */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE ENUMERATED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STRUCTURED TYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE VARIABLES DEFINITIONS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int socket_set_non_blocking(const int *socket_fd)
{
    int res = STATUS_FAILURE;

    if(socket_fd == NULL)
    {
        log_error("[socket_helper] socket_set_non_blocking: invalid input");
    }
    else
    {
        /* Get current flags */
        int flags = fcntl(*socket_fd, F_GETFL, 0);
        if(flags == -1)
        {
            log_error("[socket_helper] fcntl(F_GETFL) failed: %s", strerror(errno));
        }
        else
        {
            /* Set O_NONBLOCK flag */
            if(fcntl(*socket_fd, F_SETFL, flags | O_NONBLOCK) == -1)
            {
                log_error("[socket_helper] fcntl(F_SETFL) failed: %s", strerror(errno));
            }
            else
            {
                res = STATUS_SUCCESS;
            }
        }
    }
    return res;
}

int socket_disable_nagle(const int *socket_fd)
{
    int res = STATUS_FAILURE;
    int one = 1;

    if(socket_fd == NULL)
    {
        log_error("[socket_helper] socket_disable_nagle: invalid input");
    }
    else
    {
        if(setsockopt(*socket_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == -1)
        {
            log_error("[socket_helper] setsockopt(TCP_NODELAY) failed: %s", strerror(errno));
        }
        else
        {
            res = STATUS_SUCCESS;
        }
    }
    return res;
}

int listener_socket_init(const int *listen_fd)
{
    int res = STATUS_FAILURE;

    if(listen_fd == NULL)
    {
        log_error("[socket_helper] listener_socket_init: invalid input");
    }
    else
    {
        /* Set non-blocking mode */
        if(socket_set_non_blocking(listen_fd) == STATUS_SUCCESS)
        {
            /* Additional listener options (SO_REUSEADDR, etc.) can be set here */
            res = STATUS_SUCCESS;
        }
        else
        {
            log_error("[socket_helper] listener_socket_init: failed to set non-blocking");
        }
    }
    return res;
}

int client_socket_init(const int *client_fd)
{
    int res = STATUS_FAILURE;

    if(client_fd == NULL)
    {
        log_error("[socket_helper] client_socket_init: invalid input");
    }
    else
    {
        /* Set non-blocking mode */
        if(socket_set_non_blocking(client_fd) == STATUS_SUCCESS)
        {
            /* Disable Nagle's algorithm */
            if(socket_disable_nagle(client_fd) == STATUS_SUCCESS)
            {
                /* Additional per-client options can be set here */
                res = STATUS_SUCCESS;
            }
            else
            {
                log_error("[socket_helper] client_socket_init: failed to disable Nagle");
            }
        }
        else
        {
            log_error("[socket_helper] client_socket_init: failed to set non-blocking");
        }
    }
    return res;
}

int pipe_fd_set_non_blocking(const int *pipe_fd)
{
    /* Just a wrapper for socket_set_non_blocking */
    return socket_set_non_blocking(pipe_fd);
}
