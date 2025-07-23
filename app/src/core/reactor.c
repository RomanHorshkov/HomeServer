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

#include "epoll_helper.h"
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

struct register_t
{
    int fd;
    uint32_t mask;
    reactor_callback cb;
    void *ctx;
};

struct reactor
{
    /* epoll instance */
    int epoll_fd;

    /* epoll events for wait loop */
    struct epoll_event epoll_events[MAX_FAN_OUT_SOCKETS];

    /* register */
    struct register_t registers[MAX_CLIENTS + PIPELINE_MAX_SOCKETS];

    /* number of registers */
    int active_registers;
    /* TO DO: a hashmap from fd→(ctx, event_cb) */
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

static int find_register(reactor_t *reactor, int fd);

static int reactor_manage_fd(reactor_t *reactor, int watch_fd, int operation, uint32_t events,
                             reactor_callback cb, void *ctx);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int reactor_init(reactor_t **reactor)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(!reactor)
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
            new_reactor->epoll_fd = epoll_new();

            if(new_reactor->epoll_fd < 0)
            {
                log_error("[reactor] epoll_new() failed");
                free(new_reactor);
            }

            else
            {
                *reactor = new_reactor;
                res = STATUS_SUCCESS;
            }
        }
    }

    return res;
}

int reactor_add_in(reactor_t *reactor, int fd, reactor_callback cb, void *ctx)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    if(!reactor || fd < 0 || !cb || reactor->active_registers >= MAX_FAN_OUT_SOCKETS)
    {
        log_error("[reactor] reactor_manage_fd: invalid input");
    }

    else
    {
        res = reactor_manage_fd(reactor, fd, EPOLL_CTL_ADD, EPOLLIN, cb, ctx);
    }

    return res;
}

int reactor_add_in_client(reactor_t *reactor, int fd, reactor_callback cb, void *ctx)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    if(!reactor || fd < 0 || !cb || reactor->active_registers >= MAX_FAN_OUT_SOCKETS)
    {
        log_error("[reactor] reactor_manage_fd: invalid input");
    }

    else
    {
        res = reactor_manage_fd(reactor, fd, EPOLL_CTL_ADD,
                                EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR, cb, ctx);
    }

    return res;
}

int reactor_add_out(reactor_t *reactor, int fd, reactor_callback cb, void *ctx)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    if(!reactor || fd < 0 || !cb || reactor->active_registers >= MAX_FAN_OUT_SOCKETS)
    {
        log_error("[reactor] reactor_manage_fd: invalid input");
    }

    else
    {
        res = reactor_manage_fd(reactor, fd, EPOLL_CTL_ADD, EPOLLOUT, cb, ctx);
    }

    return res;
}

int reactor_mod(reactor_t *reactor, int fd, uint32_t events, reactor_callback cb, void *ctx)
{
    if(!reactor || fd < 0 || !cb)
    {
        log_error("[reactor] mod: invalid input");
        return STATUS_FAILURE;
    }
    return reactor_manage_fd(reactor, fd, EPOLL_CTL_MOD, events, cb, ctx);
}

int reactor_del(reactor_t *reactor, int fd)
{
    if(!reactor || fd < 0)
    {
        log_error("[reactor] del: invalid input");
        return STATUS_FAILURE;
    }
    return reactor_manage_fd(reactor, fd, EPOLL_CTL_DEL, 0, NULL, NULL);
}

int reactor_run(reactor_t *reactor, int *out_fd)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(!reactor)
    {
        log_error("[reactor] reactor_init wrong input");
    }
    else
    {
        /* Wait until some event comes */
        int n = epoll_listen_events(reactor->epoll_fd, &reactor->epoll_events);
        if(n < 0)
        {
            // if(errno == EINTR) continue;
            log_error("[reactor] reactor_run: epoll_listen_events error: %s", strerror(errno));
        }

        else
        {
            /* Check each event */
            for(int i = 0; i < n; i++)
            {
                /* Get the kernel-returned fd saved at epoll ADD instance */
                int fd = reactor->epoll_events[i].data.fd;
                uint32_t ev = reactor->epoll_events->events;

                /* Handle error/hangup/peer close events */
                if(epoll_check_if_to_close(reactor->epoll_events[i].events))
                {
#ifdef DEBUG_MODE
                    log_info("[reactor] reactor_run: epoll fd %d to close, break.", fd);
#endif
                    /* break and exit for each fd to close, signaling a shutdown to the caller  */
                    break;
                }

                /* Check if any fd matches with table */
                int idx = find_register(reactor, fd);
                if(idx >= 0)
                {
#ifdef DEBUG_MODE
                    log_info("[reactor] reactor_run: dispatching event on fd=%d, idx %d", fd, idx);
#endif
                    res = reactor->registers[idx].cb(fd, ev, reactor->registers[idx].ctx);
                }
                else
                {
#ifdef DEBUG_MODE
                    log_info("[reactor] reactor_run: NO REG ENTRY FOUND");
#endif
                }
            } /* for */
        }
    }

    return res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int find_register(reactor_t *reactor, int fd)
{
    for(int i = 0; i < reactor->active_registers; i++)
    {
        if(reactor->registers[i].fd == fd) return i;
    }
    return -1;
}

static int reactor_manage_fd(reactor_t *reactor, int watch_fd, int operation, uint32_t events,
                             reactor_callback cb, void *ctx)
{
    if(!reactor || watch_fd < 0)
    {
        log_error("[reactor] manage_fd: bad args");
        return STATUS_FAILURE;
    }

    /* 1. Let the kernel work first – if this fails we don’t mutate state. */
    if(epoll_manage_fd(reactor->epoll_fd, watch_fd, operation, events) < 0)
    {
        log_error("[reactor] manage_fd: epoll_manage_fd failed (%s)", strerror(errno));
        return STATUS_FAILURE;
    }

    /* 2. User‑space book‑keeping. */
    int idx = find_register(reactor, watch_fd);

    switch(operation)
    {
        case EPOLL_CTL_ADD:
            if(idx >= 0)
            {
                log_error("[reactor] manage_fd: fd %d already registered", watch_fd);
                return STATUS_FAILURE;
            }
            if(reactor->active_registers >= MAX_FAN_OUT_SOCKETS)
            {
                log_error("[reactor] manage_fd: register table full");
                return STATUS_FAILURE;
            }

            idx = reactor->active_registers++;
            reactor->registers[idx].fd = watch_fd;
            reactor->registers[idx].mask = events;
            reactor->registers[idx].cb = cb;
            reactor->registers[idx].ctx = ctx;
            break;

        case EPOLL_CTL_MOD:
            if(idx < 0)
            {
                log_error("[reactor] manage_fd: fd %d not found for MOD", watch_fd);
                return STATUS_FAILURE;
            }

            reactor->registers[idx].mask = events;
            reactor->registers[idx].cb = cb;
            reactor->registers[idx].ctx = ctx;
            break;

        case EPOLL_CTL_DEL:
            if(idx < 0)
            {
                log_error("[reactor] manage_fd: fd %d not found for DEL", watch_fd);
                return STATUS_FAILURE;
            }

            /* Compact the array by swapping the last entry in. */
            int last = --reactor->active_registers;
            reactor->registers[idx] = reactor->registers[last];
            memset(&reactor->registers[last], 0, sizeof(struct register_t));
            break;

        default:
            log_error("[reactor] manage_fd: unknown op %d", operation);
            return STATUS_FAILURE;
    }

    return STATUS_SUCCESS;
}
