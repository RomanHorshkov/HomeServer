#define _GNU_SOURCE

#include "pipeline.h"

#include <errno.h>       /* errno, EAGAIN, etc. */
#include <stdatomic.h>   /* atomic_int */
#include <stdlib.h>      /* malloc(), calloc(), etc */
#include <string.h>
#include <sys/eventfd.h> /* eventfd(),  */
#include <unistd.h> /* fork(), close(), pipe(), read(), write(), getlogin(), getcwd(), system() etc. */

#include "config_core.h"
#include "spsc_ring.h"
#include "socket_helper.h"
#include "emlog.h"

#define LOG_TAG "srv_pipeline"

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
struct pipeline
{
    /* W -> L control pipe */
    int pipe_fds[2];

    /* L -> W wakeup eventfd */
    int wakeup_fd;

    /** SPSC ring for L <-> W communication:
     *   - This is a single-producer, single-consumer ring buffer.
     *   - It is used to pass file descriptors (FDs) from the listener to the worker thread.
     *   - The ring buffer is initialized with a capacity defined by `SPSC_RING_CAPACITY`, which is
     * set in `server_settings.h`.
     *   - The worker thread will read FDs from this ring buffer and process them as they arrive.
     *   - The listener thread will push new client FDs into this ring buffer as they are accepted.
     */
    spsc_ring_t *ring_ptr;
};

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */

/**
 * @brief Singleton instance of the pipeline structure.
 * 
 * This variable holds the single instance of the pipeline used for communication
 * between the listener and worker threads. It encapsulates the necessary file
 * descriptors and the SPSC ring buffer for efficient data transfer.
 */

pipeline_t pipeline = {0};

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int pipeline_init(pipeline_t **pipeline_ptr)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Initialize the pipe between listener and worker */
    if(pipe(pipeline.pipe_fds) == -1)
    {
        EML_PERR(LOG_TAG, "_init: failed to create pipe");
        goto fail;
    }

    /* Set the pipe file descriptors to non-blocking */
    if(pipe_socket_init(pipeline.pipe_fds) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "_init: _socket_init failed.");
        goto fail;
    }

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
    pipeline.wakeup_fd = eventfd(0/*initial value for generic event channel*/, EFD_SEMAPHORE | EFD_NONBLOCK);

    /* Create ring object */
    spsc_ring_t *ring_ptr = spsc_ring_init(PIPELINE_SPSC_RING_CAPACITY);

    /* Check memory allocation */
    if(ring_ptr == NULL)
    {
        EML_ERROR(LOG_TAG, "_init: failed to create SPSC ring");
        goto fail;
    }

    /* Check ring health */
    pipeline.ring_ptr = ring_ptr;

    /* set return reference */
    *pipeline_ptr = &pipeline;
    return STATUS_SUCCESS;

#ifdef DEBUG_MODE
    EML_INFO(LOG_TAG, "_init: pipeline initialized successfully");
#endif

fail:
    if (ring_ptr)
    {
    spsc_ring_destroy(&ring_ptr);
    }

    return res;
}

int pipeline_push(const int client_fd)
{
    /* Check inputs */
    if(client_fd < 0)
    {
        EML_ERROR(LOG_TAG, "_push invalid input");
        goto fail;
    }

    /* Check if push on ring successful */
    if(spsc_ring_push(pipeline.ring_ptr, client_fd))
    {
        EML_ERROR(LOG_TAG, "_push spsc_ring_push failed for fd %d", client_fd);
        goto fail;
    }

    /* Send a wake-up signal */
    if(write(pipeline.wakeup_fd, &(uint64_t){1U}/* wakeup counter */, sizeof(uint64_t)) != sizeof(uint64_t))
    {
        EML_PERR(LOG_TAG, "_push write to wakeup_fd failed");
        goto fail;
    }

    return STATUS_SUCCESS;
fail:
    return STATUS_FAILURE;
}

int pipeline_pop(int *out_fd)
{
    return (status_t)spsc_ring_pop(pipeline.ring_ptr, out_fd);
}

int pipeline_notify_worker_status_change(worker_status_t status)
{
    /* Send the worker status to the listener */
    uint8_t st = (uint8_t)status;
    if(write(pipeline.pipe_fds[1], &st, sizeof(uint8_t)) != sizeof(uint8_t))
    {
        EML_PERR(LOG_TAG, "_notify_worker_status_change: write failed");
        return STATUS_FAILURE;
    }

    /* If everything went ok */
#ifdef DEBUG_MODE
    EML_DBG(LOG_TAG, " updated listener about state change %d", (int)status);
#endif /* DEBUG_MODE */
    return STATUS_SUCCESS;
}

int pipeline_get_wakeup_fd(void)
{
    return pipeline.wakeup_fd;
}

int pipeline_get_pipe_end_fd(int end)
{
    return pipeline.pipe_fds[end];
}

void pipeline_destroy(void)
{
    spsc_ring_destroy(&pipeline.ring_ptr);
    socket_shutdown_and_close(pipeline.pipe_fds[0]);
    socket_shutdown_and_close(pipeline.pipe_fds[1]);
    socket_shutdown_and_close(pipeline.wakeup_fd);
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
/* None */
