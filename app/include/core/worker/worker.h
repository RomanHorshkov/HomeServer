/**
 * @file worker.h
 * @brief APIs for managing, accepting, and tearing down server client sockets.
 *
 * This header defines the interface for the worker subsystem, which is responsible
 * for managing all active client connections. The worker receives new client sockets
 * from the listener via a non-blocking pipe, registers them with epoll, and handles
 * all client I/O in an event-driven manner.
 *
 * The worker supports scalable, concurrent client management, robust error handling,
 * and clean shutdown via atomic status flags.
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025-05-11
 */

#ifndef SERVER_WORKER_H
#define SERVER_WORKER_H

#include <stdint.h>

/****************************************************************************
 * PUBLIC TYPES
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


/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief Initialise the worker subsystem and prepare for client management.
 *
 * This function allocates and configures a new worker instance, sets up the
 * event-driven infrastructure (epoll), and prepares the worker to receive new
 * client sockets from the listener via the provided non-blocking pipe.
 *
 * On success, the worker is ready to be run in its own thread via
 * @ref worker_run(). On failure, all resources are cleaned up and it is
 * safe for the caller to terminate.
 *
 * @param worker_ptr_ptr      Address of a pointer to a worker_t; will be allocated.
 * @param pipeline_ptr        Pointer to the communication pipeline with listener.
 * @param available_cpu_count CPU count detected at startup (used to size operators).
 *
 * @retval  0  Success.
 * @retval -1  Failure (see log for details).
 */
int worker_init(uint8_t cpu_count);

int worker_dispatch_to_operator(int client_fd);

int worker_run(void);

void worker_destroy(void);

#endif /* SERVER_WORKER_H */
