/**
 * @file reactor.c
 * @brief Manages file descriptor events using epoll, dispatches callbacks, and supports
 * context-aware I/O event handling.
 *
 * This module abstracts epoll-based I/O multiplexing into a lightweight event reactor pattern. It allows registering, modifying, and
 * removing file descriptors, each with an associated callback and opaque context. Upon readiness, the reactor dispatches the events to
 * corresponding callbacks.
 *
 * Designed to be used as the event-processing core of a worker thread.
 *
 * Usage:
 *   reactor_init(...)
 *   _manage_fd(...)
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

#include "config_core.h" /* MAX_FAN_OUT_SOCKETS */
#include "emlog.h"
#include "epoller.h"

#define LOG_TAG "srv_reactor"

/*****************************************************************************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * PRIVATE ENUMERATED VARIABLES
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * PRIVATE VARIABLES DEFINITIONS
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 *****************************************************************************************************************************************
 */

static int _manage_fd(const reactor_t* reactor_ptr, const int watch_fd, const int operation, const uint32_t events, fd_ctx_t* ctx);

/*****************************************************************************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

int reactor_init(reactor_t* reactor_ptr)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(!reactor_ptr)
    {
        EML_ERROR(LOG_TAG, "_init: invalid input");
        goto fail;
    }

    /* Initialize reactor's epoller */
    reactor_ptr->epoll_fd = epoller_new();

    if(reactor_ptr->epoll_fd <= 0)
    {
        EML_PERR(LOG_TAG, "_init: epoller_new() failed");
        goto fail;
    }

    /* Allocate the events buffer (one slot per possible registration) */
    reactor_ptr->events = calloc((size_t)MAX_FAN_OUT_SOCKETS, sizeof(struct epoll_event));

    if(!reactor_ptr->events)
    {
        EML_PERR(LOG_TAG, "_init: reactor events malloc failed");
        epoller_shutdown(reactor_ptr->epoll_fd);
        goto fail;
    }

#ifdef DEBUG
    EML_DBG(LOG_TAG, "_init: reactor initialized successfully");
#endif /* DEBUG */

    return STATUS_SUCCESS;

fail:
    return res;
}

int reactor_add_in(const reactor_t* reactor_ptr, const int fd, fd_ctx_t* ctx)
{
    if(!reactor_ptr || fd < 0 || !ctx)
    {
        EML_ERROR(LOG_TAG, "_add_in: invalid input");
        return STATUS_FAILURE;
    }

    return _manage_fd(reactor_ptr, fd, EPOLL_CTL_ADD, EPOLLIN, ctx);
}

int reactor_add_in_client(const reactor_t* reactor_ptr, int fd, fd_ctx_t* ctx)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    if(!reactor_ptr || fd < 0 || !ctx)
    {
        EML_ERROR(LOG_TAG, "_add_in_client: invalid input");
        return STATUS_FAILURE;
    }

    res = _manage_fd(reactor_ptr, fd, EPOLL_CTL_ADD, EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR, ctx);

    if(res != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "_add_in_client: _manage_fd failed");
    }

    return res;
}

int reactor_add_out(const reactor_t* reactor_ptr, int fd, fd_ctx_t* ctx)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    if(!reactor_ptr || fd < 0 || !ctx)
    {
        EML_ERROR(LOG_TAG, "[reactor] _manage_fd: invalid input");
    }

    else
    {
        res = _manage_fd(reactor_ptr, fd, EPOLL_CTL_ADD, EPOLLOUT, ctx);
    }

    return res;
}

int reactor_mod(const reactor_t* reactor_ptr, int fd, uint32_t events, fd_ctx_t* ctx)
{
    if(!reactor_ptr || fd < 0 || !ctx)
    {
        EML_ERROR(LOG_TAG, "[reactor] mod: invalid input");
        return STATUS_FAILURE;
    }
    return _manage_fd(reactor_ptr, fd, EPOLL_CTL_MOD, events, ctx);
}

int reactor_del(const reactor_t* reactor_ptr, int fd)
{
    if(!reactor_ptr || fd < 0)
    {
        EML_ERROR(LOG_TAG, "[reactor] del: invalid input");
        return STATUS_FAILURE;
    }
    return _manage_fd(reactor_ptr, fd, EPOLL_CTL_DEL, 0, NULL);
}

int reactor_run(reactor_t* reactor_ptr, int* out_fd)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(!reactor_ptr)
    {
        EML_ERROR(LOG_TAG, "[reactor] _run wrong input");
        goto fail;
    }

    /* Wait until some event comes */
    int n = epoller_wait(reactor_ptr->epoll_fd, reactor_ptr->events);
    if(n <= 0)
    {
        EML_ERROR(LOG_TAG, "epoller_wait error");
        goto fail;
    }

    /* Dispatch each ready event */
    for(int i = 0; i < n; i++)
    {
        /* Get the kernel-returned ptr saved at epoll ADD instance */
        fd_ctx_t* ctx = (fd_ctx_t*)reactor_ptr->events[i].data.ptr;

        /* Handle error/hangup/peer close events */
        if(epoller_check_if_to_close(reactor_ptr->events[i].events))
        {
            /* signal the fd to close */
            *out_fd = ctx->fd;
#ifdef DEBUG
            EML_DBG(LOG_TAG, "_run: epoll fd %d to close, continue.", ctx->fd);
#endif
            goto fail;
        }

        /* Call the handler */
        res = ctx->handler(ctx->fd, ctx);

    } /* for */

    return res;
fail:
    return STATUS_FAILURE;
}

int reactor_shutdown(reactor_t* reactor_ptr)
{
    if(!reactor_ptr)
    {
        EML_ERROR(LOG_TAG, "_shutdown: invalid input");
        return STATUS_FAILURE;
    }

    /* Close epoll instance */
    if(epoller_shutdown(reactor_ptr->epoll_fd) < 0)
    {
        EML_PERR(LOG_TAG, "_shutdown: failed to close epoller fd");
    }

    /* Free the events buffer */
    if(reactor_ptr->events)
    {
        free(reactor_ptr->events);
    }

    return STATUS_SUCCESS;
}

/*****************************************************************************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

static int _manage_fd(const reactor_t* reactor_ptr, const int watch_fd, const int operation, uint32_t events, fd_ctx_t* ctx)
{
    if(operation == 0)
    {
        EML_ERROR(LOG_TAG, "_manage_fd: invalid input, operation 0");
        return STATUS_FAILURE;
    }

    /* Let the kernel work first – if this fails don’t mutate state. */
    if(epoller_manage_fd(reactor_ptr->epoll_fd, watch_fd, operation, events, (void*)ctx) < 0)
    {
        EML_PERR(LOG_TAG, "_manage_fd: epoller failed");
        return STATUS_FAILURE;
    }

    return STATUS_SUCCESS;
}
