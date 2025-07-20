#define _GNU_SOURCE

#include "pipeline.h"

#include <errno.h>       /* errno, EAGAIN, etc. */
#include <stdlib.h>      /* malloc(), calloc(), etc */
#include <sys/eventfd.h> /* eventfd(),  */
#include <unistd.h> /* fork(), close(), pipe(), read(), write(), getlogin(), getcwd(), system() etc. */

#include "logger.h"
#include "socket_helper.h"
#include "spsc_ring.h"

/**
 * @file pipeline.c
 * @brief
 *
 * Exit Codes:
 *   STATUS_SUCCESS  (0)
 *   STATUS_FAILURE  (1)
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑05‑15
 * (c) 2025
 */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE VARIABLES
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

int pipeline_init(pipeline_t **pipeline_ptr_ptr)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(pipeline_ptr_ptr == NULL)
    {
        log_error("[CORE] communication_init: invalid input");
    }

    else
    {
        /* Allocate memory for pipeline_t structure */
        *pipeline_ptr_ptr = (pipeline_t *)calloc(1, sizeof(pipeline_t));

        if(*pipeline_ptr_ptr == NULL)
        {
            log_error("[CORE] communication_init: failed to allocate memory for pipeline_t");
        }

        /* Initialize the pipe between listener and worker */
        else if(pipe((*pipeline_ptr_ptr)->pipe_fds) == -1)
        {
            log_error("[CORE] pipe failed to create: %s", strerror(errno));
        }

        /* Set the pipe file descriptors to non-blocking */
        else if(pipe_socket_init((*pipeline_ptr_ptr)->pipe_fds) != STATUS_SUCCESS)
        {
            log_error("[CORE] pipe_socket_init failed.");
        }

        else
        {
            /* Create the wake up eventfd */
            /**
             * With EFD_SEMAPHORE:
             * This changes the behavior of read():
             * Each read() returns exactly 1, not the full counter.
             * The internal counter is decremented by 1.
             * It's perfect for semaphores: multiple waiters can all wake one-by-one.
             * With EFD_NONBLOCK:
             * read() fails with -1 and errno = EAGAIN if counter is 0
             * write() fails with -1 and errno = EAGAIN if counter would overflow (very rare in
             * practice)
             */
            (*pipeline_ptr_ptr)->wakeup_fd = eventfd(0, EFD_SEMAPHORE | EFD_NONBLOCK);

            /* Create ring object */
            (*pipeline_ptr_ptr)->ring_ptr = spsc_ring_init(SPSC_RING_CAPACITY);

            /* Check memory allocation */
            if((*pipeline_ptr_ptr)->ring_ptr == NULL)
            {
                log_error("[CORE] communication_init: failed to create SPSC ring buffer");
            }
            else
            {
                res = STATUS_SUCCESS;
#ifdef DEBUG_MODE
                log_info("[pipeline] started correctly");
#endif
            }
        }
    }

    return res;
}

int pipeline_push_and_notify_worker(pipeline_t *pipeline_ptr, const int client_fd)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check inputs */
    if(pipeline_ptr == NULL || pipeline_ptr->ring_ptr == NULL || client_fd < 0)
    {
        log_error("[pipeline]: push_and_notify_worker invalid input");
    }

    /* Check if ring has free space */
    else if(spsc_ring_is_full(pipeline_ptr->ring_ptr))
    {
        log_error("[listener] spsc_ring_is_full, fd %d refused and closed", client_fd);
    }

    /* Check if push on ring successful */
    else if(spsc_ring_push(pipeline_ptr->ring_ptr, client_fd) != 0)
    {
        log_error("[pipeline]: spsc_ring_push failed for fd %d", client_fd);
    }

    /* Send a wake-up signal */
    else
    {
        /* Set wakeup counter */
        uint64_t inc = 1;

        /* Check if successfully written */
        if(write(pipeline_ptr->wakeup_fd, &inc, sizeof(uint64_t)) == sizeof(uint64_t))
        {
            /* Set return status */
            res = STATUS_SUCCESS;
        }
    }

    return res;
}

int pipeline_notify_worker_status_change(pipeline_t *pipeline, worker_status status)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(pipeline == NULL)
    {
        log_error("[pipeline] pipeline_notify_worker_status_change: invalid input");
    }

    /* Send the status to the listener */
    else if(write(pipeline->pipe_fds[1], &status, sizeof(uint32_t)) != sizeof(uint32_t))
    {
        log_error("[pipeline] pipeline_notify_worker_status_change: write failed: %s",
                  strerror(errno));
    }

    /* If everything went ok */
    else
    {
#ifdef DEBUG_MODE
        log_info("[pipeline] updated listener about state change %d", (int)status);
#endif /* DEBUG_MODE */
        res = STATUS_SUCCESS;
    }

    return res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
/* None */
