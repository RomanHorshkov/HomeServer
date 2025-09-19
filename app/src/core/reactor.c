/**
 * @file reactor.c
 * @brief Manages file descriptor events using epoll, dispatches callbacks, and supports
 * context-aware I/O event handling.
 *
 * This module abstracts epoll-based I/O multiplexing into a lightweight event reactor pattern.
 * It allows registering, modifying, and removing file descriptors, each with an associated
 * callback and opaque context. Upon readiness, the reactor dispatches the events to
 * corresponding callbacks.
 *
 * Designed to be used as the event-processing core of a worker thread.
 *
 * Usage:
 *   reactor_init(...)
 *   reactor_manage_fd(...)
 *   reactor_loop(...)
 *
 * Exit Codes:
 *   STATUS_SUCCESS  (0)
 *   STATUS_FAILURE  (1)
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑07‑21
 * (c) 2025
 */

#define _GNU_SOURCE

#include "reactor.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "epoller.h"
#include "logger.h"

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */

/**
 * @struct reactor
 * @brief Internal state of the event reactor.
 *
 * Stores epoll instance and per-event dispatch information.
 * Future extension includes tracking per-fd user context and callbacks.
 */

/* The reactor’s internal structure – opaque to users */
struct reactor
{
    /* reactor's epoll instance */
    int epoll_fd;

    /* epoll events for wait loop */
    struct epoll_event *events;
};

/****************************************************************************
 * PRIVATE ENUMERATED VARIABLES
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

static int reactor_manage_fd(const reactor_t *reactor_ptr, int watch_fd,
                             int operation, uint32_t events, fd_ctx_t *ctx);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int reactor_init(reactor_t **reactor_ptr_ptr)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(!reactor_ptr_ptr)
    {
        log_error("[reactor] reactor_init wrong input");
    }

    else
    {
        /* Allocate memory for new reactor */
        reactor_t *new_reactor = calloc(1, sizeof(reactor_t));
        if(!new_reactor)
        {
            log_error("[reactor] reactor_init calloc failed");
        }

        else
        {
            /* Initialize reactor's epoll instance */
            new_reactor->epoll_fd = epoller_new();

            if(new_reactor->epoll_fd < 0)
            {
                log_error("[reactor] epoll_new() failed %s", strerror(errno));
                free(new_reactor);
            }

            else
            {
                /* Allocate the events buffer (one slot per possible registration) */
                new_reactor->events =
                    calloc(MAX_FAN_OUT_SOCKETS, sizeof(struct epoll_event));

                if(new_reactor->events == NULL)
                {
                    log_error("[reactor] calloc() events failed %s",
                              strerror(errno));
                    free(new_reactor->events);
                    free(new_reactor);
                }

                else
                {
                    *reactor_ptr_ptr = new_reactor;
                    res              = STATUS_SUCCESS;
                }
            }
        }
    }

    return res;
}

/* VINCENZO S EXAMPLE */

// int reactor_init2(reactor_t **reactor_ptr_ptr)
// {
//     /* Result variable */
//     int res = STATUS_FAILURE;

//     /* Check input */
//     if(!reactor_ptr_ptr)
//     {
//         log_error("[reactor] reactor_init wrong input");
//         goto exit;
//     }

//     /* Allocate memory for new reactor */
//     reactor_t *new_reactor = calloc(1, sizeof(reactor_t));
//     if(!new_reactor)
//     {
//         log_error("[reactor] reactor_init calloc failed");
//         goto exit;
//     }

//     /* Initialize reactor's epoll instance */
//     new_reactor->epoll_fd = epoller_new();

//     if(new_reactor->epoll_fd < 0)
//     {
//         log_error("[reactor] epoll_new() failed %s", strerror(errno));
//         goto clean_reactor;
//     }

//     /* Allocate the events buffer (one slot per possible registration) */
//     new_reactor->events = calloc(MAX_FAN_OUT_SOCKETS, sizeof(struct epoll_event));

//     if(new_reactor->events == NULL)
//     {
//         log_error("[reactor] calloc() events failed %s", strerror(errno));
//         goto clean_events;
//     }

//     *reactor_ptr_ptr = new_reactor;
//     res = STATUS_SUCCESS;

//     return res;

// clean_events:
//     free(new_reactor->events);
// clean_reactor:
//     free(new_reactor);
// exit:
//     return res;
// }

int reactor_add_in(const reactor_t *reactor_ptr, int fd, fd_ctx_t *ctx)
{
    int res = STATUS_FAILURE;
    /* Result variable */

    if(!reactor_ptr || fd < 0 || !ctx)
    {
        log_error("[reactor] _add_in: invalid input");
    }

    else
    {
        res = reactor_manage_fd(reactor_ptr, fd, EPOLL_CTL_ADD, EPOLLIN, ctx);
        log_info("[reactor] _add_in executed with status %d", res);
    }

    return res;
}

int reactor_add_in_client(const reactor_t *reactor_ptr, int fd, fd_ctx_t *ctx)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    if(!reactor_ptr || fd < 0 || !ctx)
    {
        log_error("[reactor] _add_in_client: invalid input");
    }

    else
    {
        res =
            reactor_manage_fd(reactor_ptr, fd, EPOLL_CTL_ADD,
                              EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR, ctx);
    }

    return res;
}

int reactor_add_out(const reactor_t *reactor_ptr, int fd, fd_ctx_t *ctx)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    if(!reactor_ptr || fd < 0 || !ctx)
    {
        log_error("[reactor] reactor_manage_fd: invalid input");
    }

    else
    {
        res = reactor_manage_fd(reactor_ptr, fd, EPOLL_CTL_ADD, EPOLLOUT, ctx);
    }

    return res;
}

int reactor_mod(const reactor_t *reactor_ptr, int fd, uint32_t events,
                fd_ctx_t *ctx)
{
    if(!reactor_ptr || fd < 0 || !ctx)
    {
        log_error("[reactor] mod: invalid input");
        return STATUS_FAILURE;
    }
    return reactor_manage_fd(reactor_ptr, fd, EPOLL_CTL_MOD, events, ctx);
}

int reactor_del(const reactor_t *reactor_ptr, int fd)
{
    if(!reactor_ptr || fd < 0)
    {
        log_error("[reactor] del: invalid input");
        return STATUS_FAILURE;
    }
    return reactor_manage_fd(reactor_ptr, fd, EPOLL_CTL_DEL, 0, NULL);
}

int reactor_run(reactor_t *reactor_ptr, int *out_fd)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(!reactor_ptr)
    {
        log_error("[reactor] _run wrong input");
    }
    else
    {
        /* Wait until some event comes */
        int n = epoller_wait(reactor_ptr->epoll_fd, reactor_ptr->events);
        if(n <= 0)
        {
            log_error("[reactor] _run epoller_listen_events error %s",
                      strerror(errno));
        }

        else
        {
            /* Dispatch each ready event */
            for(int i = 0; i < n; i++)
            {
                /* Get the kernel-returned ptr saved at epoll ADD instance */
                fd_ctx_t *ctx = (fd_ctx_t *)reactor_ptr->events[i].data.ptr;

                /* Handle error/hangup/peer close events */
                if(epoller_check_if_to_close(reactor_ptr->events[i].events))
                {
                    /* signal the fd to close */
                    *out_fd = ctx->fd;
#ifdef DEBUG_MODE
                    log_info(
                        "[reactor] reactor_run: epoll fd %d to close, "
                        "continue.",
                        ctx->fd);
#endif
                    res = STATUS_FAILURE;
                    break;
                }

                else
                {
#ifdef DEBUG_MODE
                    log_info("[reactor] reactor_run: calling fd %d handler",
                             ctx->fd);
#endif
                    /* Call the handler */
                    res = ctx->handler(ctx->fd, ctx);
#ifdef DEBUG_MODE
                    log_info(
                        "[reactor] reactor_run: return after handler call res "
                        "= %d",
                        res);
#endif
                }
            } /* for */
        }
    }

    return res;
}

int reactor_shutdown(reactor_t **reactor_ptr_ptr)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    if(!reactor_ptr_ptr || !(*reactor_ptr_ptr))
    {
        log_error("[reactor] shutdown: invalid input");
    }

    else
    {
        reactor_t *reactor_ptr = *reactor_ptr_ptr;

        /* Close epoll instance */
        if(epoller_shutdown(reactor_ptr->epoll_fd) < 0)
        {
            log_error("[reactor] shutdown: failed to close epoll fd (%s)",
                      strerror(errno));
        }

        else
        {
            /* Free the events buffer */
            free(reactor_ptr->events);

            /* Free the reactor object */
            free(reactor_ptr);

            /* Nullify the caller's pointer */
            *reactor_ptr_ptr = NULL;

            res = STATUS_SUCCESS;
        }
    }

    return res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int reactor_manage_fd(const reactor_t *reactor_ptr, int watch_fd,
                             int operation, uint32_t events, fd_ctx_t *ctx)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    if(!reactor_ptr || watch_fd < 0 || operation == 0)
    {
        log_error("[reactor] manage_fd: bad args");
    }

    /* 1. Let the kernel work first – if this fails don’t mutate state. */
    else if(epoller_manage_fd(reactor_ptr->epoll_fd, watch_fd, operation,
                              events, (void *)ctx) < 0)
    {
        log_error("[reactor] manage_fd: epoll_manage_fd failed (%s)",
                  strerror(errno));
    }

    else
    {
        res = STATUS_SUCCESS;
    }

    return res;
}
