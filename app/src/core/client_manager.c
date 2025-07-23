#define _GNU_SOURCE
/**
 * @file client_manager.c
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑05‑11
 * (c) 2025
 */

#include "client_manager.h"

#include <errno.h>
#include <stdlib.h>

#include "browser.h" /* browser_manage_client_req */
#include "server_settings.h"
#include "time_helper.h"

/****************************************************************************
 * PRIVATE STRUCTURED TYPES
 ****************************************************************************
 */

typedef struct
{
    /* connection's file descriptor */
    int fd;

    /* last activity timestamp */
    int last_activity;

    /* number of requests handled */
    int request_count;

} connection_t;

struct client_manager
{
    /* NEED AN EXPANDABLE LIST */
    connection_t connections[MAX_CLIENTS];

    int active_connections;
};

/****************************************************************************
 * PRIVATE VARIABLES DEFINITIONS
 ****************************************************************************
 */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

static size_t find_fd_idx(client_manager_t *client_manager_ptr, int fd);

static int refresh_client_connection(client_manager_t *client_manager_ptr, int fd);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int client_manager_init(client_manager_t **client_manager_ptr_ptr)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(!client_manager_ptr_ptr)
    {
        log_error("[client_manager] init: invalid input");
    }
    else
    {
        /* Allocate memory */
        client_manager_t *client_manager_ptr = calloc(1, sizeof(client_manager_t));
        if(!client_manager_ptr)
        {
            log_error("[client_manager] init: calloc failed: %s", strerror(errno));
        }

        else
        {
#ifdef DEBUG_MODE
            log_info("[client_manager] initialized");
#endif
            /* Initialize the worker's connections(_t) to 0 */
            memset(client_manager_ptr->connections, 0, MAX_CLIENTS * sizeof(connection_t));
            *client_manager_ptr_ptr = client_manager_ptr;
            res = STATUS_SUCCESS;
        }
    }

    return 0;
}

int client_manager_add_connection(client_manager_t *client_manager_ptr, int fd)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(!client_manager_ptr || fd < 0)
    {
        log_error("[client_manager] _add_connection: invalid input");
    }

    /* Check available space */
    else if(client_manager_ptr->active_connections >= MAX_CLIENTS)
    {
        log_error("[client_manager] _add_connection: MAX_CLIENTS");
    }

    /* Add new connection */
    else
    {
#ifdef DEBUG_MODE
        log_info("[client_manager] _add_connection: added fd %d", fd);
#endif
        /* Find a free slot */
        size_t idx = find_fd_idx(client_manager_ptr, 0);
        if(idx < 0)
        {
            log_error("[client_manager] _add_connection: fd %d not found", 0);
        }
        else
        {
            client_manager_ptr->connections[idx].fd = fd;
            client_manager_ptr->connections[idx].last_activity = time_helper_get_now();
            client_manager_ptr->connections[idx].request_count = 0;
            client_manager_ptr->active_connections++;
        }

        res = STATUS_SUCCESS;
    }

    return res;
}

int client_manager_remove_connection(client_manager_t *client_manager_ptr, int fd)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(!client_manager_ptr || fd < 0)
    {
        log_error("[client_manager] _remove_connection: invalid input");
    }

    else
    {
        ssize_t idx = find_fd_idx(client_manager_ptr, fd);
        if(idx < 0)
        {
            log_error("[client_manager] _remove_connection: fd %d not found", fd);
        }
        else
        {
#ifdef DEBUG_MODE
            log_info("[client_manager] _remove_connection: swap connection at idx %d with last",
                     idx);
#endif
            /* Swap‑pop */
            client_manager_ptr->connections[idx] =
                client_manager_ptr->connections[--client_manager_ptr->active_connections];
            /* Zero out the unused slot */
            memset(&client_manager_ptr->connections[client_manager_ptr->active_connections], 0,
                   sizeof(connection_t));
            res = STATUS_SUCCESS;
        }
    }

    return res;
}

int client_manager_manage_client(client_manager_t *client_manager_ptr, int fd)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(!client_manager_ptr || fd < 0)
    {
        log_error("[client_manager] _manage_client: invalid input");
    }

    /* Process the client's request */
    else if(browser_manage_client_req(fd) != STATUS_SUCCESS)
    {
        log_error("[client_manager] _manage_client: browser_manage_client_req failed for fd %d",
                  fd);
    }

    /* Refresh connection's request_count and last_activity */
    else if(refresh_client_connection(client_manager_ptr, fd) != STATUS_SUCCESS)
    {
        log_error("[client_manager] _manage_client: refresh_client_connection failed for fd %d",
                  fd);
    }

    else
    {
#ifdef DEBUG_MODE
        log_info("[client_manager] _manage_client: ALL OK on fd %d", fd);
#endif
        res = STATUS_SUCCESS;
    }

    return res;
}

// int client_manager_cleanup_idle(client_manager_t *client_manager_ptr, cm_idle_cb_t cb,
//                                 void *ctx)
// {
//     /* Result variable */
//     int res = STATUS_FAILURE;

//     /* Check input */
//     if(!client_manager_ptr || !cb)
//     {
//         log_error("[client_manager] refresh_client_connection: invalid input");
//     }
//     time_t now = time(NULL);
//     int cleaned = 0;

//     for(size_t i = 0; i < client_manager_ptr->len; /* no increment */)
//     {
//         time_t delta = now - client_manager_ptr->conns[i].last_activity;
//         if((uint32_t)delta > timeout_s)
//         {
//             int fd = client_manager_ptr->conns[i].fd;
//             // invoke user callback
//             cb(fd, ctx);
//             // remove by swap‑pop
//             client_manager_ptr->conns[i] = client_manager_ptr->conns[--client_manager_ptr->len];
//             cleaned++;
//         }
//         else
//         {
//             i++;
//         }
//     }
//     return cleaned;
// }

int client_manager_get_active_connections(client_manager_t *client_manager_ptr)
{
    return client_manager_ptr ? client_manager_ptr->active_connections : -1;
}

int client_manager_shutdown(client_manager_t *client_manager_ptr)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(!client_manager_ptr)
    {
        log_error("[client_manager] _shutdown: invalid input");
    }
    else
    {
        free(client_manager_ptr);
        res = STATUS_SUCCESS;
    }

    return res;
}
/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int refresh_client_connection(client_manager_t *client_manager_ptr, int fd)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(!client_manager_ptr || fd < 0)
    {
        log_error("[client_manager] refresh_client_connection: invalid input");
    }

    else
    {
        ssize_t idx = find_fd_idx(client_manager_ptr, fd);
        if(idx < 0)
        {
            log_error("[client_manager_ptr] refresh: fd %d not found", fd);
        }
        else
        {
#ifdef DEBUG_MODE
            log_info("[client_manager] refresh_client_connection: ok");
#endif
            client_manager_ptr->connections[idx].last_activity = time_helper_get_now();
            client_manager_ptr->connections[idx].request_count++;
            res = STATUS_SUCCESS;
        }
    }

    return res;
}

static size_t find_fd_idx(client_manager_t *client_manager_ptr, int fd)
{
    for(size_t i = 0; i < MAX_CLIENTS; i++)
    {
        if(client_manager_ptr->connections[i].fd == fd) return i;
    }
    return -1;
}
