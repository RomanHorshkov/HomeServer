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

#include "client_manager.h"
#include "logger.h" /* logger */
#include "reactor.h"
#include "server_settings.h"
#include "socket_helper.h"
#include "time_helper.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */

typedef _Atomic worker_status worker_status_t;

/* This structure holds the worker's data, including epoll and timer instances,
 * connections, active connections count, pipe read file descriptor, and status.
 */
struct worker
{
    /* status variable */
    worker_status_t status; /* 0 = inactive, 1 = active */

    /* reactor instance */
    reactor_t *reactor_ptr;

    /* timer instance */
    int timer_fd;

    /* connection_manager */
    client_manager_t *client_manager_ptr;

    /* collaboration with listener structure */
    pipeline_t *pipeline_ptr;
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

static int delete_connection(worker_t *worker_ptr, int fd);

static int manage_time_event(int fd, void *worker);

static int manage_wakeup_event(int fd, void *worker);

static int manage_client_event(int fd, void *worker);

static int worker_check_status_change(worker_t *worker_ptr, worker_status status);

static int worker_set_status_and_update_listener(worker_t *worker_ptr, worker_status status);

static int timer_init(worker_t *worker_ptr, int *timer_fd);

static void timer_update(worker_t *worker_ptr);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int worker_init(worker_t **worker_ptr_ptr, pipeline_t *pipeline_ptr)
{
    /* return value */
    int res = STATUS_FAILURE;

    /* Check input */
    if(worker_ptr_ptr == NULL || pipeline_ptr == NULL)
    {
        log_error("[worker]:worker_init: invalid input");
    }
    else
    {
        /* Allocate memory for the new worker */
        worker_t *new_worker = (worker_t *)calloc(1, sizeof(worker_t));

        int wakeup_fd = -1;

        /* Check memory allocation */
        if(new_worker == NULL)
        {
            log_error("[worker]:worker_init: memory allocation failed: %s", strerror(errno));
        }

        /* Create reactor */
        else if(reactor_init(&new_worker->reactor_ptr) != STATUS_SUCCESS)
        {
            log_error("[worker]:worker_init: reactor_init failed: %s", strerror(errno));
        }

        /* Initialize worker's timer and register to reactor */
        else if(timer_init(new_worker, &new_worker->timer_fd) != STATUS_SUCCESS)
        {
            log_error("[worker]: worker_init: timer_init failed: %s", strerror(errno));
        }

        /* Get wakeup fd from pipeline */
        else if((wakeup_fd = pipeline_get_wakeup_fd(pipeline_ptr)) < 0)
        {
            log_error("[worker]: worker_init: pipeline_get_wakeup_fd failed: %s, wakeup_fd %d", strerror(errno), wakeup_fd);
        }

        /* Register wakeup_fd to reactor */
        else if(reactor_add_in(new_worker->reactor_ptr, wakeup_fd, manage_wakeup_event, (void*)new_worker) == -1)
        {
            log_error("[worker]: worker_init: epoll_add_in failed wakeup_fd: %s", strerror(errno));
        }

        else if(client_manager_init(&new_worker->client_manager_ptr))
        {
            log_error("[worker]: worker_init: client_manager_init failed: %s", strerror(errno));
        }
        else
        {
            /* Initialize the worker's communication pipeline */
            new_worker->pipeline_ptr = pipeline_ptr;

            /* Initialize the worker's status and update the listener */
            // if(worker_set_status_and_update_listener(
            //         new_worker, (worker_status)WORKER_STATUS_ACTIVE) != STATUS_SUCCESS)
            if(worker_set_status(new_worker, (worker_status)WORKER_STATUS_ACTIVE) != STATUS_SUCCESS)
            {
                log_error(
                    "[worker]: worker_init: worker_set_status failed "
                    "%s",
                    strerror(errno));
            }

            else
            {
                res = STATUS_SUCCESS;
                *worker_ptr_ptr = new_worker;
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
        /** Let the reactor react to events:
         * waits on epoll for any event
         * checks error/hangup/peer close events
         * calls registered callback
         */
        int out_fd = -1;
        if(reactor_run(worker_ptr->reactor_ptr, &out_fd) != STATUS_SUCCESS)
        {
            /* If the reactor did not run successfully, check if a socket has to be closed */
            if(out_fd != -1)
            {
                delete_connection(worker_ptr, out_fd);
                out_fd = -1;
            }
            else
            {
                log_error("[worker] reactor_run failed: %s", strerror(errno));
            }
        }
    }

    /**
     *
     * TO CLOSE EVERYTHING HERE IF WORKER SHUTSDOWN!!!
     *  Close the epoll file descriptor */
    // close(worker_ptr->epoll_fd);

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
        log_error("[worker] worker_set_status: invalid status value %d", status);
    }

    else
    {
        /* Set the status */
        atomic_store(&worker_ptr->status, status);

        res = STATUS_SUCCESS;

#ifdef DEBUG_MODE
        /* Log the status change */
        log_info("[worker] worker_set_status: status set to %d", status);
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
    if(worker_ptr == NULL || worker_ptr->pipeline_ptr == NULL ||
       worker_ptr->client_manager_ptr == NULL)
    {
        log_error("[worker] add_connection: invalid input");
    }

    else
    {
        /* Client file descriptor to read from the pipeline */
        int client_fd = -1;

        /* Drain every fd the listener pushed into the ring */
        while((client_fd = pipeline_pop(worker_ptr->pipeline_ptr)) != STATUS_FAILURE)
        {
            /**
             * Add client to client manager */
            log_info("[worker] pipeline popped %d", client_fd);

            if(client_manager_add_connection(worker_ptr->client_manager_ptr, client_fd) !=
               STATUS_SUCCESS)
            {
                res = WORKER_STATUS_FULL;
                log_error(
                    "[worker] add_connections: client_manager_add_connection: failed, break, "
                    "RETURN FULL");

                /* This might set the worker to FULL status, break without trying to add other
                 * connections*/
                break;
            }

            /**
             * Add client fd to reactor */
            else if(reactor_add_in_client(worker_ptr->reactor_ptr, client_fd, manage_client_event,
                                          worker_ptr) != STATUS_SUCCESS)
            {
                /* Delete from client_manager */
                client_manager_remove_connection(worker_ptr->client_manager_ptr, client_fd);
                res = WORKER_STATUS_FULL;

                log_error(
                    "[worker] add_connection: Failed to add client fd %d to client_manager: %s",
                    client_fd, strerror(errno));

                /* This might set the worker to FULL status, break without trying to add other
                 * connections*/
                break;
            }

            else
            {
                res = WORKER_STATUS_ACTIVE;
            }
        }
    }

    return res;
}

static int delete_connection(worker_t *worker_ptr, int fd)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(worker_ptr == NULL || worker_ptr->pipeline_ptr == NULL ||
       worker_ptr->client_manager_ptr == NULL)
    {
        log_error("[worker] delete_connection: invalid input");
    }

    else if(client_manager_remove_connection(worker_ptr->client_manager_ptr, fd))
    {
        log_error("[worker] delete_connection: client_manager_remove_connection failed fd %d", fd);
    }

    else if(reactor_del(worker_ptr->reactor_ptr, fd) != STATUS_SUCCESS)
    {
        log_error("[worker] delete_connection: reactor_del failed fd %d", fd);
    }

    else
    {
        res = STATUS_SUCCESS;
#ifdef DEBUG_MODE
        log_info("[worker] delete_connection: fd %d removed successfully", fd);
#endif
    }

    return res;
}

static int manage_time_event(int fd, void *worker)
{
#ifdef DEBUG_MODE
    log_info("[worker] IN manage_time_event");
#endif /* DEBUG_MODE */

    /* Result variable */
    int res = STATUS_FAILURE;

    /* worker variable */
    worker_t *worker_ptr = (worker_t *)worker;

    if(socket_drain(fd) == -1)
    {
        log_error("[worker] Failed to read timerfd: %s", strerror(errno));
    }
    else
    {
        /* Update the timer */
        timer_update(worker_ptr);

        res = STATUS_SUCCESS;
    }

    log_info("[worker] in manage_time_event, status: %d", (int)atomic_load(&worker_ptr->status));

    return res;
}

static int manage_wakeup_event(int fd, void *worker)
{
#ifdef DEBUG_MODE
    log_info("[worker] IN manage_wakeup_event");
#endif /* DEBUG_MODE */

    /* Result variable */
    int res = STATUS_FAILURE;

    /* worker variable */
    worker_t *worker_ptr = (worker_t *)worker;

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
    if(worker_check_status_change(worker_ptr, w_status) != STATUS_SUCCESS)
    {
        log_error("[worker] worker_check_status_change FAILED ");
    }

    else
    {
        res = STATUS_SUCCESS;
    }

    return res;
}

static int manage_client_event(int fd, void *worker)
{
#ifdef DEBUG_MODE
    log_info("[worker] IN manage_client_event");
#endif /* DEBUG_MODE */

    /* worker variable */
    worker_t *worker_ptr = (worker_t *)worker;
    return client_manager_manage_client(worker_ptr->client_manager_ptr, fd);
}

static int worker_check_status_change(worker_t *worker_ptr, worker_status status)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Static variable to hold the previous worker's status */
    static worker_status worker_old_status = 1;

    if(worker_old_status != status)
    {
#ifdef DEBUG_MODE
        log_info("[worker] worker_check_status_change STATUS_CHANGE DETECTED new %d, old %d",
                 status, worker_old_status);
#endif

        /* Update the old status */
        worker_old_status = status;

        /* Check the new status */
        switch(status)
        {
            case WORKER_STATUS_ACTIVE:
                if(worker_set_status_and_update_listener(worker_ptr, status) != STATUS_SUCCESS)
                {
                    log_error(
                        "[worker] worker_check_status_change worker_set_status_and_update_listener "
                        "failed");
                }
                else
                {
                    res = STATUS_SUCCESS;
                }
                break;

            case WORKER_STATUS_FULL:

                if(worker_set_status_and_update_listener(worker_ptr, status) != STATUS_SUCCESS)
                {
                    log_error(
                        "[worker] worker_check_status_change worker_set_status_and_update_listener "
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

                log_error("[worker] worker_check_status_change INVALID WORKER STATUS %d",
                          (uint)status);
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

static int timer_init(worker_t *worker_ptr, int *timer_fd)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* reactor instance */
    reactor_t *reactor = worker_ptr->reactor_ptr;

    /* Check input */
    if(timer_fd == NULL)
    {
        log_error("[worker] time_helper_init: invalid input");
    }
    else
    {
        /* Create a timer fd */
        *timer_fd = time_helper_init();

        if(*timer_fd == -1)
        {
            log_error("[worker] time_helper_init: timerfd_create failed: %s", strerror(errno));
        }

        /* Set the timer interval and initial expiry */
        else if(time_helper_set(*timer_fd, SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE, 0) == -1)
        {
            log_error("[worker] time_helper_init: time_helper_set failed: %s, timer closed.",
                      strerror(errno));
            close(*timer_fd);
        }

        /* Register timer_fd into epoll */
        else if(reactor_add_in(reactor, *timer_fd, manage_time_event, worker_ptr) == -1)
        {
            log_error("[worker]: worker_init: reactor_add_in failed timer_fd: %s", strerror(errno));
        }

        else
        {
            /* Set return to success */
            res = STATUS_SUCCESS;
        }
    }

    return res;
}

static void timer_update(worker_t *worker_ptr)
{
    if(worker_ptr == NULL)
    {
        log_error("[worker] timer_update: invalid input");
    }

    /* TO DO
    DO NOT SEND TIMER UPDATE AT EVERY TIMER WAKEUP */

    /* Set timer with no connections */
    else if(client_manager_get_active_connections(worker_ptr->client_manager_ptr) <= 0)
    {
        time_helper_set(worker_ptr->timer_fd, SERVER_KEEPALIVE_TIMEOUT_ALONE, 0);
#ifdef DEBUG_MODE
        log_info(
            "[worker] timer_update: Updated timer to SERVER_KEEPALIVE_TIMEOUT_ALONE (%lu "
            "seconds)",
            SERVER_KEEPALIVE_TIMEOUT_ALONE);
#endif /* DEBUG_MODE */
    }

    /* Set timer with connections */
    else
    {
        time_helper_set(worker_ptr->timer_fd, SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE, 0);
#ifdef DEBUG_MODE
        log_info(
            "[worker] timer_update: Updated timer to SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE "
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

    /* writes the pipe */
    else if(pipeline_notify_worker_status_change(worker_ptr->pipeline_ptr, status) !=
            STATUS_SUCCESS)
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
