#define _GNU_SOURCE

#include "pipeline.h"

#include <errno.h>       /* errno, EAGAIN, etc. */
#include <stdlib.h>      /* malloc(), calloc(), etc */
#include <sys/eventfd.h> /* eventfd(),  */
#include <unistd.h> /* fork(), close(), pipe(), read(), write(), getlogin(), getcwd(), system() etc. */

#include "logger.h"
#include "socket_helper.h"

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

            /* Allocate memory for the ring */
            (*pipeline_ptr_ptr)->ring_ptr = spsc_ring_init(SPSC_RING_CAPACITY);

            /* Check memory allocation */
            if((*pipeline_ptr_ptr)->ring_ptr == NULL)
            {
                log_error("[CORE] communication_init: failed to create SPSC ring buffer");
            }
            else
            {
                res = STATUS_SUCCESS;
            }
        }
    }

    return res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
/* None */
