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
#include "pipeline.h" /* pipeline */

/****************************************************************************
 * PUBLIC TYPES
 ****************************************************************************
 */
typedef struct worker worker_t;

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
int worker_init(worker_t **worker_ptr_ptr, pipeline_t *pipeline_ptr, uint8_t available_cpu_count);

/**
 * @brief Main worker thread function: manages all active client sockets.
 *
 * This function should be run in a dedicated thread. It waits for new client
 * sockets from the listener (via the pipe), registers them with epoll, and
 * handles all client I/O in an event-driven loop. When a client disconnects
 * or an error occurs, the worker cleans up the socket and removes it from epoll.
 *
 * The loop continues until the worker's atomic status flag is set to shutdown.
 *
 * @param arg  Pointer to a worker_t instance.
 * @return     NULL on exit.
 */
void *worker_run(void *arg);

/**
 * @brief Release worker resources and owned threads.
 *
 * @param worker_ptr Pointer to the worker instance.
 */
void worker_destroy(worker_t *worker_ptr);

#endif /* SERVER_WORKER_H */
