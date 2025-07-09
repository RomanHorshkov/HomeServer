/**
 * @file socket_helper.h
 * @brief Helper functions for socket configuration and management.
 *
 * Provides wrappers for setting socket options such as non-blocking mode,
 * disabling Nagle's algorithm, and initializing listener/client sockets.
 *
 * @author  Roman Horshkov
 * @date    2025-05-11
 */

#pragma once

#include <stddef.h>
#include <sys/types.h>

/**
 * @brief Set a socket to non-blocking mode.
 *
 * Uses fcntl() to set the O_NONBLOCK flag on the given file descriptor.
 *
 * @param socket_fd  File descriptor of the socket.
 * @retval  0  Success.
 * @retval -1 Failure (see log for details).
 */
int socket_set_non_blocking(const int *socket_fd);

/**
 * @brief Disable Nagle's algorithm (TCP_NODELAY) on a socket.
 *
 * Sets TCP_NODELAY to 1 for lower latency on the given TCP socket.
 *
 * @param socket_fd  File descriptor of the socket.
 * @retval  0  Success.
 * @retval -1 Failure (see log for details).
 */
int socket_disable_nagle(const int *socket_fd);

/**
 * @brief Initialize a listener socket with recommended options.
 *
 * Sets non-blocking mode and other options suitable for a listening socket.
 *
 * @param listen_fd  File descriptor of the listener socket.
 * @retval  0  Success.
 * @retval -1 Failure (see log for details).
 */
int listener_socket_init(const int *listen_fd);

/**
 * @brief Initialize a client socket with recommended options.
 *
 * Sets non-blocking mode and disables Nagle's algorithm for the client socket.
 *
 * @param client_fd  File descriptor of the client socket.
 * @retval  0  Success.
 * @retval -1 Failure (see log for details).
 */
int client_socket_init(const int *client_fd);

/**
 * @brief Set a pipe file descriptor to non-blocking mode.
 *
 * Uses fcntl() to set O_NONBLOCK on the pipe file descriptor.
 *
 * @param pipe_fd  File descriptor of the pipe.
 * @retval  0  Success.
 * @retval -1 Failure (see log for details).
 */
int pipe_fd_set_non_blocking(const int *pipe_fd);
