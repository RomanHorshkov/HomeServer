/**
 * @file worker.c
 * @brief Manages client connections passed from the listener, handles requests, and handles
 * concurrency.
 *
 * This module waits for client file descriptors passed through a pipe, monitors them using epoll,
 * and delegates request processing to the browser subsystem. It gracefully closes client
 * connections on error or shutdown.
 *
 * Usage:
 *   worker_init(...)
 *   worker_run(...)
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
#define _GNU_SOURCE
#include "worker.h" /* worker */

#include <fcntl.h> /* fcntl(), F_GETFL, F_SETFL, O_NONBLOCK */
// #include <netdb.h>       /* getaddrinfo(), addrinfo, gai_strerror() */
#include <stdatomic.h> /* atomic_int */
#include <stdlib.h>    /* malloc(), calloc() etc */
#include <unistd.h>    /* fork(), close(), read(), write(), etc. */

#include "browser.h" /* browser_manage_client_req */
#include "logger.h"  /* logger */
#include "utils.h"   /* utils */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */

typedef _Atomic worker_status atomic_worker_status_t;

typedef struct
{
    /* connection's file descriptor */
    int fd;

    /* last activity timestamp */
    int last_activity;

    /* number of requests handled */
    int request_count;

} connection_t;

/* This structure holds the worker's data, including epoll and timer instances,
 * connections, active connections count, pipe read file descriptor, and status.
 */
struct worker
{
    /* status variable */
    atomic_worker_status_t status; /* 0 = inactive, 1 = active */

    /* epoll instance */
    int epoll_fd;

    /* timer instance */
    int timer_fd;

    /* connections */
    connection_t connections[MAX_CLIENTS];

    /* active connections */
    uint active_connections_no;

    /* collaboration with listener structure */
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

static worker_status add_connections(worker_t *worker_ptr);

static int worker_check_status(worker_t *worker_ptr, worker_status status);

static int delete_connection(worker_t *worker_ptr, int fd);

static int update_connection(worker_t *worker_ptr, int fd);

static int manage_time_event(worker_t *worker_ptr, int fd);

static int manage_wakeup_event(worker_t *worker_ptr, int fd);

static int worker_set_status_and_update_listener(worker_t *worker_ptr, worker_status status);

static int worker_timer_init(int *timer_fd);

static void worker_timer_update(worker_t *worker_ptr);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int worker_init(worker_t **worker_ptr, pipeline_t *pipeline_ptr)
{
    /* return value */
    int res = STATUS_FAILURE;

    /* Check input */
    if(worker_ptr == NULL || pipeline_ptr == NULL)
    {
        log_error("[worker]:worker_init: invalid input");
    }
    else
    {
        /* Allocate memory for the worker */
        *worker_ptr = (worker_t *)calloc(1, sizeof(worker_t));

        /* Check memory allocation */
        if(*worker_ptr == NULL)
        {
            log_error("[worker]:worker_init: memory allocation failed: %s", strerror(errno));
        }

        else
        {
            /* Create an epoll instance */
            int epoll_fd = epoll_new();

            /* Check epoll instance */
            if(epoll_fd == -1)
            {
                log_error("[worker]: worker_init: epoll_create1 failed: %s", strerror(errno));
            }

            else
            {
                /* Set the worker's epoll instance */
                (*worker_ptr)->epoll_fd = epoll_fd;

                /* Initialize worker's timer */
                if(worker_timer_init(&(*worker_ptr)->timer_fd) != STATUS_SUCCESS)
                {
                    log_error("[worker]: worker_init: timer_init failed: %s", strerror(errno));
                }

                /* Register timer_fd into epoll */
                else if(epoll_add_in((*worker_ptr)->epoll_fd, (*worker_ptr)->timer_fd) == -1)
                {
                    log_error("[worker]: worker_init: epoll_add_in failed timer_fd: %s",
                              strerror(errno));
                }

                /* Register wakeup_fd into epoll */
                else if(epoll_add_in((*worker_ptr)->epoll_fd, pipeline_ptr->wakeup_fd) == -1)
                {
                    log_error("[worker]: worker_init: epoll_add_in failed wakeup_fd: %s",
                              strerror(errno));
                }
                else
                {
                    /* Initialize the worker's connections(_t) */
                    memset((*worker_ptr)->connections, 0, MAX_CLIENTS * sizeof(connection_t));

                    /* Initialize the worker's active sockets number */
                    (*worker_ptr)->active_connections_no = 0;

                    /* Initialize the worker's communication pipeline */
                    (*worker_ptr)->pipeline = pipeline_ptr;

                    /* Initialize the worker's status and update the listener */
                    if(worker_set_status_and_update_listener(
                           *worker_ptr, (worker_status)WORKER_STATUS_ACTIVE) == STATUS_FAILURE)
                    {
                        log_error(
                            "[worker]: worker_init: worker_set_status_and_update_listener failed "
                            "%s",
                            strerror(errno));
                    }

                    else
                    {
                        res = STATUS_SUCCESS;
                    }
                }
            }
        }
    }

    return res;
}

void *worker_run(void *arg)
{
    /* Check input */
    if(arg == NULL)
    {
        log_error("[worker]: worker_run: invalid input");
        return NULL;
    }

    /* make a local copy of the worker ptr */
    worker_t *worker_ptr = (worker_t *)arg;

    worker_status worker_status = atomic_load(&worker_ptr->status);

    /* Main worker thread loop */
    while(worker_status == WORKER_STATUS_ACTIVE || worker_status == WORKER_STATUS_FULL)
    {
        epoll_events_t evnts;
        /* Wait for events on the pipe or client sockets.
        Specifying a timeout of -1 causes epoll_wait() to block indefinitely */
        int nfds = epoll_listen_events(worker_ptr->epoll_fd, &evnts);
        // int nfds = epoll_wait(worker_ptr->epoll_fd, events, MAX_FAN_OUT_SOCKETS, -1);

        /* Check for errors */
        if(nfds < 0)
        {
            log_error("[worker]: worker_run: epoll_wait failed: %s", strerror(errno));
            continue;
        }

        /* Process all ready events */
        for(int i = 0; i < nfds; i++)
        {
            /* Update always worker's status after a possible connection deletion */
            worker_check_status(worker_ptr, WORKER_STATUS_ACTIVE);

            /* Get the kernel-returned fd saved at epoll ADD instance */
            int fd = epoll_get_event_fd(&evnts, i);
            uint32_t ev_conn = epoll_get_event_type(&evnts, i);

            /* Handle error/hangup/peer close events */
            if(epoll_check_if_to_close(ev_conn))
            {
                delete_connection(worker_ptr, fd);
                continue;
            }

            /* Event is on the timer handle logic */
            if(fd == worker_ptr->timer_fd)
            {
                manage_time_event(worker_ptr, fd);
            }

            /* Event on the write pipe */
            else if(fd == worker_ptr->pipeline->pipe_fds[1])
            {
#ifdef DEBUG_MODE
                log_info("[worker] pipe read side is ready to write");
#endif /* DEBUG_MODE */
            }

            /* Event on wakeup_fd from listener, check the ring for new clients */
            else if(fd == worker_ptr->pipeline->wakeup_fd)
            {
                manage_wakeup_event(worker_ptr, fd);
                continue;
            }

            /* Otherwise, the event is on a client socket */
            else
            {
                /* Refresh connection's request_count and last_activity */
                if(update_connection(worker_ptr, fd) != STATUS_SUCCESS)
                {
                    log_error("[worker] update_connection failed for fd %d", fd);
                    delete_connection(worker_ptr, fd);
                }

                /* Manage the client request through the browser */
                else if(browser_manage_client_req(fd) != STATUS_SUCCESS)
                {
                    log_error("[worker]: browser failed fd %d, closing it.", fd);
                    delete_connection(worker_ptr, fd);
                }
            }
        }
    } /* while(SERVER_STATUS_ACTIVE) */

    /* Close the epoll file descriptor */
    close(worker_ptr->epoll_fd);

    return NULL;
}

int worker_set_status(worker_t *worker_ptr, worker_status status)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    if(worker_ptr == NULL)
    {
        log_error("[worker] worker_set_status: invalid listener pointer");
    }

    else if(status >= WORKER_STATUS_INVALID)
    {
        log_error("[worker] worker_set_status_and_update_listener: invalid status value %d",
                  status);
    }

    else
    {
        /* Set the status */
        atomic_store(&worker_ptr->status, status);

        res = STATUS_SUCCESS;

#ifdef DEBUG_MODE
        /* Log the status change */
        log_info("[worker] status set to %d", status);
#endif /* DEBUG_MODE */
    }

    return res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static worker_status add_connections(worker_t *worker_ptr)
{
    /* Return variable */
    worker_status res = WORKER_STATUS_ERROR;

    /* Check input */
    if(worker_ptr == NULL || worker_ptr->pipeline == NULL || worker_ptr->pipeline->ring_ptr == NULL)
    {
        log_error("[worker] add_connection: invalid input");
    }

    else if(worker_ptr->active_connections_no < MAX_CLIENTS)
    {
        /* Client file descriptor to read from the ring */
        int client_fd;

        /* Drain every fd the listener pushed into the ring */
        while(!spsc_ring_is_empty(worker_ptr->pipeline->ring_ptr) &&
              spsc_ring_pop(worker_ptr->pipeline->ring_ptr, &client_fd) != -1)
        {
            /* Find a free slot */
            int slot = -1;
            for(uint i = 0; i < MAX_CLIENTS; ++i)
            {
                if(worker_ptr->connections[i].fd == 0)
                {
                    slot = i;
                    break;
                }
            }

            /* Check if a free slot was found */
            if(slot == -1)
            {
                log_error("[worker] add_connection: slot not found for fd %d", client_fd);
                /* no free slot, something quite wrong happened here */
                break;
            }

            /* Add the new client fd to epoll */
            if(epoll_add_in_client(worker_ptr->epoll_fd, client_fd) == -1)
            {
                log_error("[worker] Failed to add client fd %d to epoll: %s", client_fd,
                          strerror(errno));
            }

            /* Initialise bookkeeping */
            else
            {
                /* set the connection's details */
                connection_t *c = &worker_ptr->connections[slot];

                /* set connection's client fd */
                c->fd = client_fd;

                /* set connection's last activity to now */
                c->last_activity = timer_get_now();

                /* set connection's request count to 0 */
                c->request_count = 0;

                /* Increment active connections no */
                worker_ptr->active_connections_no++;

                /* set return to success */
                res = WORKER_STATUS_ACTIVE;
            }

            /* Check the worker's status about max connections allowed */
            if(worker_ptr->active_connections_no >= MAX_CLIENTS)
            {
#ifdef DEBUG_MODE
                log_info("[worker] add_connection: Maximum number of connections reached 1: %d",
                         MAX_CLIENTS);
#endif /* DEBUG_MODE */
                res = WORKER_STATUS_FULL;

                /* Exit the loop if max connections reached */
                break;
            }
        }
    }
    else
    {
#ifdef DEBUG_MODE
        log_info("[worker] add_connection: Maximum number of connections reached 2: %d",
                 MAX_CLIENTS);
#endif /* DEBUG_MODE */
        res = WORKER_STATUS_FULL;
    }

    return res;
}

static int worker_check_status(worker_t *worker_ptr, worker_status status)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Static variable to hold the previous worker's status */
    static worker_status worker_old_status = 1;

    if(worker_old_status != status)
    {
#ifdef DEBUG_MODE
        log_info("[worker] worker_check_status STATUS_CHANGE DETECTED new %d, old %d", status,
                 worker_old_status);
#endif

        /* Update the old status */
        worker_old_status = status;

        /* Check the new status */
        switch(status)
        {
            case WORKER_STATUS_ACTIVE:
                if(worker_ptr->active_connections_no < MAX_CLIENTS &&
                   worker_set_status_and_update_listener(worker_ptr, status) != STATUS_SUCCESS)
                {
                    log_error(
                        "[worker] worker_check_status worker_set_status_and_update_listener "
                        "failed");
                }
                else
                {
                    res = STATUS_SUCCESS;
                }
                break;

            case WORKER_STATUS_FULL:

                if(worker_ptr->active_connections_no >= MAX_CLIENTS &&
                   worker_set_status_and_update_listener(worker_ptr, status) != STATUS_SUCCESS)
                {
                    log_error(
                        "[worker] worker_check_status worker_set_status_and_update_listener "
                        "failed");
                }
                else
                {
                    res = STATUS_SUCCESS;
                }
                break;

            case WORKER_STATUS_ERROR:
            case WORKER_STATUS_INACTIVE:
            case WORKER_STATUS_INVALID:
            default:

                log_error("[worker] worker_check_status INVALID WORKER STATUS %d", (uint)status);
                break;
        }
    }

    /* No status change detected */
    else
    {
        res = STATUS_SUCCESS;
    }

    return res;
}

static int delete_connection(worker_t *worker_ptr, int fd)
{
    int res = STATUS_FAILURE;
    if(worker_ptr == NULL)
    {
        log_error("[worker] delete_connection: invalid input");
    }
    else
    {
        for(uint j = 0; j < MAX_CLIENTS; ++j)
        {
            if(worker_ptr->connections[j].fd == fd)
            {
                /* Remove from epoll */
                epoll_del(worker_ptr->epoll_fd, fd);

                /* send FIN – graceful half-close and close the socket */
                socket_shutdown_and_close(fd);

                /* Reset the connection slot */
                worker_ptr->connections[j].fd = 0;
                worker_ptr->connections[j].last_activity = 0;
                worker_ptr->connections[j].request_count = 0;
                worker_ptr->active_connections_no--;

                res = STATUS_SUCCESS;
#ifdef DEBUG_MODE
                log_info("[worker] delete_connection: fd %d removed and slot reset", fd);
#endif
                break;
            }
        }
    }

    return res;
}

static int update_connection(worker_t *worker_ptr, int fd)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(worker_ptr == NULL)
    {
        log_error("[worker] update_connection: invalid input");
    }

    else
    {
        /* TO DO : HASH TABLE */
        /* Find the connection and update it */
        for(uint j = 0; j < MAX_CLIENTS; ++j)
        {
            if(worker_ptr->connections[j].fd == fd)
            {
                worker_ptr->connections[j].last_activity = timer_get_now();
                worker_ptr->connections[j].request_count++;
                res = STATUS_SUCCESS;
                break;
            }
        }
    }

    return res;
}

static int worker_timer_init(int *timer_fd)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(timer_fd == NULL)
    {
        log_error("[worker] worker_timer_init: invalid input");
    }
    else
    {
        /* Create a timer fd */
        *timer_fd = timer_init();

        if(*timer_fd == -1)
        {
            log_error("[worker] worker_timer_init: timerfd_create failed: %s", strerror(errno));
        }

        /* Set the timer interval and initial expiry */
        else if(timer_set(*timer_fd, SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE, 0) == -1)
        {
            log_error("[worker] worker_timer_init: timer_set failed: %s, timer closed.",
                      strerror(errno));
            close(*timer_fd);
        }
        else
        {
            /* Set return to success */
            res = STATUS_SUCCESS;
        }
    }

    return res;
}

static int manage_time_event(worker_t *worker_ptr, int fd)
{
    /* Result variable */
    int res = STATUS_FAILURE;

#ifdef DEBUG_MODE
    log_info("[worker] Timer event received");
#endif /* DEBUG_MODE */

    if(socket_drain(fd) == -1)
    {
        log_error("[worker] Failed to read timerfd: %s", strerror(errno));
    }
    else
    {
        /* Update the timer */
        worker_timer_update(worker_ptr);

        res = STATUS_SUCCESS;
    }

    return res;
}

static int manage_wakeup_event(worker_t *worker_ptr, int fd)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Clean the wakeup_fd with read and proceed with ring reading */

#ifdef DEBUG_MODE
    uint64_t wakeup_count;
    ssize_t s = read(fd, &wakeup_count, sizeof(wakeup_count));
    log_info("[worker] Wakeup event received on fd %d, wakeup_count %ld, size %ld", fd,
             wakeup_count, s);
#else
    socket_drain(fd);
#endif /* DEBUG_MODE */

    /* loop the ring and add new connections */
    worker_status w_status = add_connections(worker_ptr);

    /* Check if the worker's status changed */
    if(worker_check_status(worker_ptr, w_status) != STATUS_SUCCESS)
    {
        log_error("[worker] worker_check_status FAILED ");
    }

    else
    {
        res = STATUS_SUCCESS;
    }

    return res;
}

static void worker_timer_update(worker_t *worker_ptr)
{
    if(worker_ptr == NULL) return;

    /* TO DO
    DO NOT SEND TIMER UPDATE AT EVERY TIMER WAKEUP */

    /* Set timer with no connections */
    if(worker_ptr->active_connections_no == 0)
    {
        timer_set(worker_ptr->timer_fd, SERVER_KEEPALIVE_TIMEOUT_ALONE, 0);
#ifdef DEBUG_MODE
        log_info(
            "[worker] worker_timer_update: Updated timer to SERVER_KEEPALIVE_TIMEOUT_ALONE (%lu "
            "seconds)",
            SERVER_KEEPALIVE_TIMEOUT_ALONE);
#endif /* DEBUG_MODE */
    }

    /* Set timer with connections */
    else if(worker_ptr->active_connections_no > 0)
    {
        timer_set(worker_ptr->timer_fd, SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE, 0);
#ifdef DEBUG_MODE
        log_info(
            "[worker] worker_timer_update: Updated timer to SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE "
            "(%lu seconds)",
            SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE);
#endif /* DEBUG_MODE */
    }
}

static int worker_set_status_and_update_listener(worker_t *worker_ptr, worker_status status)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(worker_ptr == NULL)
    {
        log_error("worker_set_status_and_update_listener: invalid worker pointer");
    }

    else if(worker_set_status(worker_ptr, status) != STATUS_SUCCESS)
    {
        log_error("worker_set_status_and_update_listener: worker_set_status failed");
    }

    /* writes the pipe after calling atomic_store(), If the listener thread pre‑empts between those
     * calls, it can read a “new” status while status still holds the old value. */
    else if(pipeline_notify_worker_status_change(worker_ptr->pipeline, status) != STATUS_SUCCESS)
    {
        log_error(
            "worker_set_status_and_update_listener: pipeline_notify_worker_status_change failed");
    }

    else
    {
        res = STATUS_SUCCESS;
    }

    return res;
}
