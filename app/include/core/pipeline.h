
/**
 * @file pipeline.h
 * @brief
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025-05-15
 */

#ifndef SERVER_PIPELINE_H
#define SERVER_PIPELINE_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */
#include "server_settings.h"
#include "spsc_ring.h"

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC STRUCTURES
 ****************************************************************************
 */

typedef struct
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

} pipeline_t;

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

int pipeline_init(pipeline_t **pipeline_ptr_ptr);

int pipeline_push_and_notify_worker(pipeline_t *pipeline_ptr, const int client_fd);

#endif /* SERVER_PIPELINE_H */
