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

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#define _GNU_SOURCE
#include "worker.h" /* worker */

#include <fcntl.h> /* fcntl(), F_GETFL, F_SETFL, O_NONBLOCK */
// #include <netdb.h>       /* getaddrinfo(), addrinfo, gai_strerror() */
#include <stdatomic.h> /* atomic_int */
#include <stdlib.h>    /* malloc(), calloc() etc */
#include <unistd.h>    /* fork(), close(), read(), write(), etc. */

#include "browser.h"
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

typedef struct
{
    /* connection's file descriptor */
    int fd;

    /* reactor context */
    fd_ctx_t *ctx;

    /* last activity timestamp */
    int last_activity;

    /* number of requests handled */
    int request_count;

} client_slot_t;

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

    /* collaboration with listener structure */
    pipeline_t *pipeline_ptr;

    /* clients array */
    client_slot_t clients[MAX_CLIENTS];

    /* number of active clients */
    size_t active_clients;
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

static int handle_wakeup_event(int fd, fd_ctx_t *ctx);

static int handle_client_event(int fd, fd_ctx_t *ctx);

static int handle_timer_event(int fd, fd_ctx_t *ctx);

/* Helpers */
static int add_client(worker_t *worker_ptr, int client_fd);

static int remove_client(worker_t *worker_ptr, int client_fd);
// static void notify_listener(worker_t *w, worker_status st);

static int timer_init(worker_t *worker_ptr, int *timer_fd);

static void timer_update(const worker_t *worker_ptr);

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

        /* Check memory allocation */
        if(new_worker == NULL)
        {
            log_error("[worker]:worker_init: memory allocation failed: %s", strerror(errno));
        }

        /* Create reactor */
        else if(reactor_init(&new_worker->reactor_ptr) != STATUS_SUCCESS)
        {
            log_error("[worker]:worker_init: reactor_init failed: %s", strerror(errno));
            free(new_worker);
        }

        /* Initialize worker's timer and register to reactor */
        else if(timer_init(new_worker, &new_worker->timer_fd) != STATUS_SUCCESS)
        {
            log_error("[worker]: worker_init: timer_init failed: %s", strerror(errno));
            free(new_worker);
        }

        else
        {
            /* Initialize the worker's communication pipeline */
            new_worker->pipeline_ptr = pipeline_ptr;

            int wakeup_fd = pipeline_get_wakeup_fd(new_worker->pipeline_ptr);

            /* Get wakeup fd from pipeline */
            if(wakeup_fd < 0)
            {
                log_error(
                    "[worker]: worker_init: pipeline_get_wakeup_fd failed: %s, "
                    "wakeup_fd %d",
                    strerror(errno), wakeup_fd);
            }

            /* Register wakeup_fd to reactor */
            else if(reactor_add_in(new_worker->reactor_ptr, wakeup_fd,
                                   (fd_ctx_t *)&(fd_ctx_t){.fd = wakeup_fd,
                                                           .owner = new_worker,
                                                           .handler = handle_wakeup_event}) !=
                    STATUS_SUCCESS)
            {
                log_error("[worker]: worker_init: epoll_add_in failed wakeup_fd: %s",
                          strerror(errno));
            }

            /* Initialize the worker's status and update the listener */
            else if(worker_set_status(new_worker, (worker_status)WORKER_STATUS_ACTIVE) !=
                    STATUS_SUCCESS)
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
        int out_fd = -1;
        if(reactor_run(worker_ptr->reactor_ptr, &out_fd) != STATUS_SUCCESS)
        {
            /* If the reactor did not run successfully, check if a socket has to be closed */
            if(out_fd != -1)
            {
                log_info("[worker]: reactor signaled to remove fd %d, removing...", out_fd);

                remove_client(worker_ptr, out_fd);
                out_fd = -1;
            }
            else
            {
                log_error("[worker] reactor_run failed: %s", strerror(errno));
            }
        }

        /* update worker's status */
        worker_status = atomic_load(&worker_ptr->status);
    }

    /* Cleanup all clients */
    for(size_t i = 0; i < worker_ptr->active_clients; i++)
    {
        close(worker_ptr->clients[i].fd);
        free(worker_ptr->clients[i].ctx);
    }

    /* Tear down timer, reactor */
    reactor_shutdown(&worker_ptr->reactor_ptr);
    close(worker_ptr->timer_fd);
    free(worker_ptr);

    return NULL;
}

int worker_set_status(worker_t *worker_ptr, worker_status status)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    if(worker_ptr == NULL)
    {
        log_error("[worker] _set_status: invalid listener pointer");
    }

    else if(status >= WORKER_STATUS_INVALID)
    {
        log_error("[worker] _set_status: invalid status value %d", status);
    }

    else
    {
        /* Set the status */
        atomic_store(&worker_ptr->status, status);

        res = STATUS_SUCCESS;

#ifdef DEBUG_MODE
        /* Log the status change */
        log_info("[worker] _set_status: status set to %d", status);
#endif /* DEBUG_MODE */

        /* writes the pipe */
        if(pipeline_notify_worker_status_change(worker_ptr->pipeline_ptr, status) != STATUS_SUCCESS)
        {
            log_error(
                "[worker] _set_status: pipeline_notify "
                "failed");
        }
    }

    return res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int handle_wakeup_event(int fd, fd_ctx_t *ctx)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* worker variable */
    worker_t *worker_ptr = (worker_t *)ctx->owner;

    /* Clean the wakeup_fd with read and proceed with ring reading */
    socket_drain(fd);

    /* Client file descriptor to read from the pipeline */
    int client_fd = -1;

    /* Drain every fd the listener pushed into the ring */
    while((client_fd = pipeline_pop(worker_ptr->pipeline_ptr)) != STATUS_FAILURE)
    {
        if(add_client(worker_ptr, client_fd) != STATUS_SUCCESS)
        {
            log_error("[worker] on_wakeup: add_client failed, closing %d", client_fd);
            socket_shutdown_and_close(client_fd);
            worker_set_status(worker_ptr, WORKER_STATUS_FULL);
        }
        else
        {
            res = STATUS_SUCCESS;
        }
    }

    /* If added at least one and were FULL before, maybe go back ACTIVE */
    if(worker_ptr->active_clients < MAX_CLIENTS &&
       atomic_load(&worker_ptr->status) == WORKER_STATUS_FULL)
    {
        worker_set_status(worker_ptr, WORKER_STATUS_ACTIVE);
    }

    return res;
}

static int handle_client_event(int fd, fd_ctx_t *ctx)
{
    (void)ctx;
    return browser_manage_client_req(fd);
}

static int handle_timer_event(int fd, fd_ctx_t *ctx)
{
#ifdef DEBUG_MODE
    log_info("[worker] IN manage_time_event");
#endif /* DEBUG_MODE */

    /* Result variable */
    int res = STATUS_FAILURE;

    /* worker variable */
    const worker_t *worker_ptr = (worker_t *)ctx->owner;

    /* Drain the socket to shutdownn epoll wait */
    if(socket_drain(fd) == -1)
    {
        log_error("[worker] Failed to read timerfd: %s", strerror(errno));
    }

    /* Update the timer */
    else
    {
        timer_update(worker_ptr);

        // time_t now = time_helper_get_now();
        // for(size_t i = 0; i < worker_ptr->active_clients;)
        // {
        //     if((now - worker_ptr->clients[i].last_activity) > CLIENT_TIMEOUT_S)
        //     {
        //         int cfd = worker_ptr->clients[i].fd;
        //         socket_shutdown_and_close(cfd);
        //         remove_client(worker_ptr, i);
        //     }
        //     else
        //     {
        //         i++;
        //     }
        // }

        res = STATUS_SUCCESS;
    }

    return res;
}

static int add_client(worker_t *worker_ptr, int client_fd)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    if(worker_ptr->active_clients >= MAX_CLIENTS)
    {
        log_error("[worker] add_client MAX_CLIENTS %d reached", MAX_CLIENTS);
    }
    else
    {
        /* make non-blocking */
        client_socket_init(&client_fd);

        /* prepare context */
        fd_ctx_t *ctx = calloc(1, sizeof(*ctx));
        ctx->fd = client_fd;
        ctx->owner = worker_ptr;
        ctx->handler = handle_client_event;

        /* register for read + hangup detection */
        if(reactor_add_in_client(worker_ptr->reactor_ptr, client_fd, ctx) != STATUS_SUCCESS)
        {
            free(ctx);
            log_error("[worker] add_client reactor_add_in_client failed");
        }
        else
        {
            /* store in slot */
            int slot = client_fd - CLIENT_FIRST_SOCKET;
            worker_ptr->clients[slot] = (client_slot_t){
                .fd = client_fd, .ctx = ctx, .last_activity = time_helper_get_now()};

            worker_ptr->active_clients++;
            res = STATUS_SUCCESS;
        }
    }
    return res;
}

static int remove_client(worker_t *worker_ptr, int client_fd)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    int slot = client_fd - CLIENT_FIRST_SOCKET;

    /* Check input */
    if(worker_ptr == NULL || client_fd < CLIENT_FIRST_SOCKET)
    {
        log_error("[worker] remove_client: invalid input");
    }

    else if(reactor_del(worker_ptr->reactor_ptr, worker_ptr->clients[slot].fd) != STATUS_SUCCESS)
    {
        log_error("[worker] remove_client: reactor_del failed fd %d at slot %d",
                  worker_ptr->clients[slot].fd, slot);
    }

    else
    {
        free(worker_ptr->clients[slot].ctx);
        worker_ptr->clients[slot].fd = -1;
        worker_ptr->clients[slot].ctx = NULL;
        worker_ptr->clients[slot].last_activity = 0;
        worker_ptr->clients[slot].request_count = 0;
        worker_ptr->active_clients--;

        res = STATUS_SUCCESS;
#ifdef DEBUG_MODE
        log_info("[worker] remove_client: fd %d @ %d removed successfully", client_fd, slot);
#endif
    }

    return res;
}

static int timer_init(worker_t *worker_ptr, int *timer_fd)
{
    /* Return variable */
    int res = STATUS_FAILURE;

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

        else
        {
            /* Create timer context for epoll kernel save */
            fd_ctx_t *timer_ctx = calloc(1, sizeof(*timer_ctx));

            if(!timer_ctx)
            {
                log_error("[worker] time_helper_init: timer_ctx failed: %s", strerror(errno));
            }
            else
            {
                timer_ctx->fd = *timer_fd;
                timer_ctx->owner = worker_ptr;
                timer_ctx->handler = handle_timer_event;

                /* Set the timer interval and initial expiry */
                if(time_helper_set(*timer_fd, SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE, 0) == -1)
                {
                    log_error(
                        "[worker] time_helper_init: time_helper_set failed: "
                        "%s, timer closed.",
                        strerror(errno));
                    close(*timer_fd);
                }

                /* Register timer into epoll */
                else if(reactor_add_in(worker_ptr->reactor_ptr, *timer_fd, timer_ctx) !=
                        STATUS_SUCCESS)
                {
                    log_error(
                        "[worker]: worker_init: reactor_add_in failed "
                        "timer_fd: %s",
                        strerror(errno));
                }

                /* Set return to success */
                else
                {
                    res = STATUS_SUCCESS;
                }
            }
        }
    }

    return res;
}

static void timer_update(const worker_t *worker_ptr)
{
    static int last_timer_update = SERVER_KEEPALIVE_TIMEOUT_ALONE;

    if(worker_ptr == NULL)
    {
        log_error("[worker] timer_update: invalid input");
    }

    /* Set timer with connections */
    else if(worker_ptr->active_clients > 0 &&
            last_timer_update != SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE)
    {
        last_timer_update = SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE;
        time_helper_set(worker_ptr->timer_fd, last_timer_update, 0);
#ifdef DEBUG_MODE
        log_info("[worker] timer_update: Updated timer (%d seconds)", last_timer_update);
#endif /* DEBUG_MODE */
    }

    /* Set timer with no connections */
    else if(last_timer_update != SERVER_KEEPALIVE_TIMEOUT_ALONE)
    {
        last_timer_update = SERVER_KEEPALIVE_TIMEOUT_ALONE;
        time_helper_set(worker_ptr->timer_fd, last_timer_update, 0);
#ifdef DEBUG_MODE
        log_info("[worker] timer_update: Updated timer to (%d seconds)", last_timer_update);
#endif /* DEBUG_MODE */
    }

    else
    {
        /* Do nothing */
    }
}
