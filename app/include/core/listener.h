
/**
 * @file listener.h
 * @brief APIs for setting up, accepting, and tearing down server listener sockets.
 *
 * This header exposes the interface for the listener subsystem, which is responsible
 * for creating, configuring, and managing all server-side listening sockets. The
 * listener accepts new client connections and forwards them to the worker thread
 * via a non-blocking pipe.
 *
 * The listener supports dual-stack IPv4/IPv6 operation, robust socket options,
 * and clean shutdown via atomic status flags.
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025-05-11
 */

#ifndef SERVER_LISTENER_H
#define SERVER_LISTENER_H

#include "pipeline.h" /* pipeline */

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
 */

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief Initialise the listener subsystem and create all listening sockets.
 *
 * This function allocates and configures a new listener instance, sets up
 * one or more listening sockets (supporting both IPv4 and IPv6 as available),
 * and prepares the listener to accept new client connections.
 *
 * The listener is given the write end of a non-blocking pipe, which it uses
 * to forward accepted client file descriptors to the worker thread.
 *
 * On success, the listener is ready to be run in its own thread via
 * @ref listener_run(). On failure, all resources are cleaned up and it is
 * safe for the caller to terminate.
 *
 * @param port             NUL-terminated string with the TCP port/service to bind.
 * @param pipe_write_fd    Pointer to the write end of the listener→worker pipe.
 *
 * @retval  0  Success.
 * @retval -1  Failure (see log for details).
 */
int listener_init(const char *port, pipeline_t *pipeline_ptr);

/**
 * @brief Main listener thread function: accepts new connections and forwards them.
 *
 * This function should be run in a dedicated thread. It registers all active
 * listening sockets with epoll, waits for incoming connections, accepts them,
 * and writes the resulting client file descriptors to the worker via the pipe.
 *
 * The loop continues until the listener's atomic status flag is set to shutdown.
 *
 * @param arg  Pointer to a listener_t instance.
 * @return     NULL on exit.
 */
void *listener_run(void *arg);

#endif /* SERVER_LISTENER_H */
