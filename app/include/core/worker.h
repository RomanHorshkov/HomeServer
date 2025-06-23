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

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief Opaque worker structure (defined in worker.c).
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
 * @param worker_ptr     Address of a pointer to a worker_t; will be allocated.
 * @param pipe_read_fd   Pointer to the read end of the listener→worker pipe.
 *
 * @retval  0  Success.
 * @retval -1  Failure (see log for details).
 */
int worker_init(worker_t **worker_ptr, int *pipe_read_fd);

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
 * @brief Set the worker's status flag (active/shutdown).
 *
 * This function is used by the core or control thread to signal the worker
 * to shut down gracefully.
 *
 * @param worker_ptr  Pointer to the worker instance.
 * @param status      New status value (e.g., SERVER_STATUS_ACTIVE or _SHUTDOWN).
 */
void worker_set_status(worker_t *worker_ptr, int status);

#endif /* SERVER_WORKER_H */
