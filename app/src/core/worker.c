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
#include "reactor.h"
#include "config_core.h"    /* core_config */
#include "socket_helper.h"
#include "time_helper.h"
#include <emlog.h>

#define LOG_TAG "srv_worker"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */

typedef enum
{
    WORKER_STATUS_INACTIVE = 0, /* worker is inactive */
    WORKER_STATUS_ACTIVE = 1,   /* worker is active */
    WORKER_STATUS_FULL = 2,     /* worker is full */
    WORKER_STATUS_SHUTDOWN = 3, /* worker to shutdown */
    WORKER_STATUS_INVALID = 4,  /* max value for worker status */
} worker_status_t;

typedef struct
{
    /* connection's file descriptor */
    int fd;

    /* reactor context */
    fd_ctx_t *ctx;

    /* last activity timestamp */
    uint32_t last_activity;

    /* number of requests handled */
    uint32_t request_count;

} client_slot_t;

/* This structure holds the worker's data, including epoll and timer instances,
 * connections, active connections count, pipe read file descriptor, and status.
 */
typedef struct
{
    /* status variable */
    worker_status_t status; /* 0 = inactive, 1 = active */

    /* worker's reactor instance */
    reactor_t reactor;

    /* timer instance */
    int timer_fd;

    /* timer freq */
    uint32_t timer_frequency;

    /* collaboration with listener structure */
    pipeline_t *pipeline_ptr;

    /* reactor context for the pipeline wakeup fd */
    fd_ctx_t *wakeup_ctx;

    /* clients array */
    client_slot_t clients[WORKER_MAX_CLIENTS];

    /* number of active clients */
    size_t active_clients;
} worker_t;


/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */

/**
 * @brief Singleton instance of the main worker structure.
 * 
 * This instance is used to manage the worker's state and operations.
 */
static worker_t worker = {0};

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

static int _handle_wakeup_event(int fd, fd_ctx_t *ctx);

static int _handle_client_event(int fd, fd_ctx_t *ctx);

static int _handle_timer_event(int fd, fd_ctx_t *ctx);

/* Helpers */
static int _add_client(worker_t *worker_ptr, int client_fd);

static int _remove_client(int client_fd);
// static void notify_listener(worker_t *w, worker_status st);

static int _timer_init(int *timer_fd);

static void _timer_update(void);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int worker_init(worker_t **worker_ptr_ptr, pipeline_t *pipeline_ptr)
{
    /* Check input */
    if(worker_ptr_ptr == NULL || pipeline_ptr == NULL)
    {
        EML_ERROR(LOG_TAG, "_init: invalid input");
        goto fail;
    }

    /* Create reactor */
    if(reactor_init(&worker.reactor, (size_t)MAX_FAN_OUT_SOCKETS) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "_init: reactor_init failed");
        goto fail;
    }

    /* Initialize worker's timer and register to reactor */
    if(_timer_init(&worker.timer_fd) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "_init: _timer_init failed");
        goto fail;
    }

    /* Initialize the worker's communication pipeline */
    worker.pipeline_ptr = pipeline_ptr;

    int wakeup_fd = pipeline_get_wakeup_fd(pipeline_ptr);

    /* Get wakeup fd from pipeline */
    if(wakeup_fd < 0)
    {
        EML_PERR(LOG_TAG, "[worker]: worker_init: invalid wakeup_fd from pipeline");
        goto fail;
    }

    /* Register wakeup_fd to reactor */
    fd_ctx_t *wakeup_ctx = calloc(1, sizeof(*wakeup_ctx));
    if(!wakeup_ctx)
    {
        EML_PERR(LOG_TAG, "[worker]: worker_init: calloc failed for wakeup_ctx");
        goto fail;
    }

    /* Setup wakeup fd context */
    wakeup_ctx->fd = wakeup_fd;
    wakeup_ctx->owner = &worker;
    wakeup_ctx->handler = _handle_wakeup_event;

    if(reactor_add_in(&worker.reactor, wakeup_fd, wakeup_ctx) !=
        STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "[worker]: worker_init: reactor_add_in failed for wakeup_fd");
        goto fail;
    }

    worker.wakeup_ctx = wakeup_ctx;

    /* Initialize the worker's status and update the listener */
    if(worker_set_status(WORKER_STATUS_ACTIVE) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "_init: worker_set_status failed");
        goto fail;
    }

    /*  */
    *worker_ptr_ptr = &worker;

    return STATUS_SUCCESS;

fail:
    return STATUS_FAILURE;
}

void *worker_run(void *arg)
{
    /* Check input */
    if(arg == NULL)
    {
        EML_ERROR(LOG_TAG, "_run: invalid input");
        return NULL;
    }

    /* make a local copy of the worker ptr */
    if ((worker_t *)arg != &worker)
    {
        EML_ERROR(LOG_TAG, "_run: invalid worker instance");
        return NULL;
    }

    /* Main worker thread loop */
    while(worker.status == WORKER_STATUS_ACTIVE || worker.status == WORKER_STATUS_FULL)
    {
        int out_fd = -1;
        if(reactor_run(&worker.reactor, &out_fd) != STATUS_SUCCESS)
        {
            /* If the reactor did not run successfully, check if a socket has to be closed */
            if(out_fd != -1)
            {
                EML_INFO(LOG_TAG, "reactor signaled to remove fd %d, removing...", out_fd);

                _remove_client(out_fd);
                out_fd = -1;
                continue;
            }
            EML_ERROR(LOG_TAG, "reactor_run failed");
        }
    }

    /* Cleanup all clients */
    for(size_t i = 0; i < worker.active_clients; i++)
    {
        close(worker.clients[i].fd);
        free(worker.clients[i].ctx);
    }

    /* Tear down timer, reactor */
    reactor_shutdown(&worker.reactor_ptr);
    close(worker.timer_fd);
    free(worker.wakeup_ctx);

    return NULL;
}

int worker_set_status(worker_status_t status)
{
    if(status >= WORKER_STATUS_INVALID)
    {
        EML_ERROR(LOG_TAG, "_set_status: invalid status value %d", status);
        goto fail;
    }

    /* Set the status */
    atomic_store(worker.status, status);

    /* writes the pipe */
    if(pipeline_notify_worker_status_change((uint8_t)status) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "_set_status: pipeline_notify failed");
        goto fail;
    }

#ifdef DEBUG_MODE
    /* Log the status change */
    EML_DBG(LOG_TAG, "_set_status: status set to %d, pipeline notified", (int)status);
#endif /* DEBUG_MODE */
    
return STATUS_SUCCESS;

fail:
    return STATUS_FAILURE;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int _handle_wakeup_event(int fd, fd_ctx_t *ctx)
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
    while((client_fd = pipeline_pop(&client_fd)) != STATUS_FAILURE)
    {
        if(_add_client(client_fd) != STATUS_SUCCESS)
        {
            EML_ERROR(LOG_TAG, "_wakeup_event: _add_client failed, closing %d", client_fd);
            socket_shutdown_and_close(client_fd);
            worker_set_status(worker_ptr, WORKER_STATUS_FULL);
        }
        else
        {
            res = STATUS_SUCCESS;
        }
    }

    /* If added at least one and were FULL before, maybe go back ACTIVE */
    if(worker_ptr->active_clients < WORKER_MAX_CLIENTS &&
       atomic_load(&worker_ptr->status) == WORKER_STATUS_FULL)
    {
        worker_set_status(worker_ptr, WORKER_STATUS_ACTIVE);
    }

    return res;
}

static int _handle_client_event(int fd, fd_ctx_t *ctx)
{
    (void)ctx;
    return browser_manage_client_req(fd);
}

static int _handle_timer_event(int fd, fd_ctx_t *ctx)
{
#ifdef DEBUG_MODE
    EML_INFO(LOG_TAG, "[worker] IN manage_time_event");
#endif /* DEBUG_MODE */

    /* Result variable */
    int res = STATUS_FAILURE;

    /* worker variable */
    const worker_t *worker_ptr = (worker_t *)ctx->owner;

    /* Drain the socket to shutdownn epoll wait */
    if(socket_drain(fd) == -1)
    {
        EML_ERROR(LOG_TAG, "[worker] Failed to read timerfd: %s", strerror(errno));
    }

    /* Update the timer */
    else
    {
        _timer_update(worker_ptr);

        // time_t now = time_helper_get_now();
        // for(size_t i = 0; i < worker_ptr->active_clients;)
        // {
        //     if((now - worker_ptr->clients[i].last_activity) > WORKER_CLIENT_TIMEOUT_SHORT)
        //     {
        //         int cfd = worker_ptr->clients[i].fd;
        //         socket_shutdown_and_close(cfd);
        //         _remove_client(worker_ptr, i);
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

static int _add_client(int client_fd)
{
    if(worker.active_clients >= WORKER_MAX_CLIENTS)
    {
        EML_WARN(LOG_TAG, "_add_client WORKER_MAX_CLIENTS %d reached", WORKER_MAX_CLIENTS);
        EML_WARN(LOG_TAG, "_add_client rejecting fd %d", client_fd);
        EML_WARN(LOG_TAG, "_add_client setting worker status to FULL");
        worker_set_status(WORKER_STATUS_FULL);
        goto fail;
    }
    /* make non-blocking */
    client_socket_init(&client_fd);

    /* prepare context */
    fd_ctx_t *ctx = calloc(1, sizeof(*ctx));
    ctx->fd = client_fd;
    ctx->owner = &worker;
    ctx->handler = _handle_client_event;

    /* register for read + hangup detection */
    if(reactor_add_in_client(&worker.reactor, client_fd, ctx) != STATUS_SUCCESS)
    {
        free(ctx);
        EML_ERROR(LOG_TAG, "[worker] _add_client reactor_add_in_client failed");
    }
    /* store in slot */
    worker.clients[active_clients++] = (client_slot_t) {
                        .fd = client_fd,
                        .ctx = ctx,
                        .last_activity = (uint32_t)time_helper_get_now()
                        .request_count = 1;
                    };

    return STATUS_SUCCESS;
fail:
    return STATUS_FAILURE;
}

static int _remove_client(int client_fd)
{
    int slot = client_fd - CLIENT_FIRST_SOCKET;

    /* Check input */
    if(client_fd < CLIENT_FIRST_SOCKET)
    {
        EML_ERROR(LOG_TAG, "[worker] _remove_client: invalid input");
        goto fail;
    }

    if(reactor_del(&worker.reactor, worker.clients[slot].fd) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "[worker] _remove_client: reactor_del failed fd %d at slot %d",
                  worker_ptr->clients[slot].fd, slot);
        goto fail;
    }

    free(worker_ptr->clients[slot].ctx);
    worker_ptr->clients[slot].fd = -1;
    worker_ptr->clients[slot].ctx = NULL;
    worker_ptr->clients[slot].last_activity = 0;
    worker_ptr->clients[slot].request_count = 0;
    worker_ptr->active_clients--;

#ifdef DEBUG_MODE
    EML_DBG(LOG_TAG, "[worker] _remove_client: fd %d @ %d removed successfully", client_fd, slot);
#endif

    return STATUS_SUCCESS;

fail:
    return STATUS_FAILURE;
}

static int _timer_init(int *timer_fd)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(timer_fd == NULL)
    {
        EML_ERROR(LOG_TAG, "_timer_init: invalid input");
        goto fail;
    }

    /* Create a timer fd */
    *timer_fd = time_helper_init();

    if(*timer_fd == -1)
    {
        EML_PERR(LOG_TAG, "_timer_init: timerfd_create failed");
        goto fail;
    }

    /* Create timer context for epoll kernel save */
    fd_ctx_t *timer_ctx = calloc(1, sizeof(*timer_ctx));

    if(!timer_ctx)
    {
        EML_PERR(LOG_TAG, "_timer_init: timer_ctx failed");
        goto fail;
    }
    
    /* Fill timer context */
    timer_ctx->fd = *timer_fd;
    timer_ctx->owner = &worker;
    timer_ctx->handler = _handle_timer_event;

    /* Set the timer interval and initial expiry */
    if(time_helper_set(*timer_fd, SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE, 0) == -1)
    {
        EML_PERR(LOG_TAG, "_timer_init: time_helper_set failed: timer closed.");
        goto fail_timer;
    }

    /* Register timer into epoll */
    if(reactor_add_in(&worker.reactor, *timer_fd, timer_ctx) !=
            STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "_timer_init: reactor_add_in failed: timer closed.");
        goto fail_timer;
    }

    return STATUS_SUCCESS;
fail_timer:
    close(*timer_fd);
fail:
    return STATUS_FAILURE;
}

static void _timer_update(void)
{
    /* Set timer with connections */
    if(worker.active_clients > 0 &&
        worker.timer_frequency != SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE)
    {
        time_helper_set(worker.timer_fd, (uint32_t)SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE, 0);
    }

    /* Set timer with no connections */
    else if(worker.active_clients == 0 &&
            worker.timer_frequency != SERVER_KEEPALIVE_TIMEOUT_ALONE)
    {
        time_helper_set(worker.timer_fd, (uint32_t)SERVER_KEEPALIVE_TIMEOUT_ALONE, 0);
    }

    // else
    // {
    //     /* Do nothing */
    // }
    
#ifdef DEBUG_MODE
    EML_DBG(LOG_TAG, "_timer_update: Updated timer (%d seconds)", worker.timer_frequency);
#endif /* DEBUG_MODE */

    return;
}
