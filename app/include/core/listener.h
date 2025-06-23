
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

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief Opaque listener structure (defined in listener.c).
 */
typedef struct listener listener_t;

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
 * @param listener_ptr     Address of a pointer to a listener_t; will be allocated.
 * @param port             NUL-terminated string with the TCP port/service to bind.
 * @param pipe_write_fd    Pointer to the write end of the listener→worker pipe.
 *
 * @retval  0  Success.
 * @retval -1  Failure (see log for details).
 */
int listener_init(listener_t **listener_ptr, const char *port, int *pipe_write_fd);

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

/**
 * @brief Set the listener's atomic status flag (active/shutdown).
 *
 * This function is used by the core or control thread to signal the listener
 * to shut down gracefully.
 *
 * @param listener_ptr  Pointer to the listener instance.
 * @param status        New status value (e.g., SERVER_STATUS_ACTIVE or _SHUTDOWN).
 */
void listener_set_status(listener_t *listener_ptr, int status);

/**
 * @brief Set a socket file descriptor to non-blocking mode.
 *
 * Utility function to set the O_NONBLOCK flag on a socket.
 *
 * @param socket_fd  Pointer to the socket file descriptor.
 * @retval  0  Success.
 * @retval -1  Failure (see log for details).
 */
int set_socket_non_blocking(const int *socket_fd);

#endif /* SERVER_LISTENER_H */
