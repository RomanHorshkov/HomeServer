#ifndef SERVER_SOCKET_HELPER_H
#define SERVER_SOCKET_HELPER_H
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

#include <stdint.h> /* int64_t, int32_t */
#include <stddef.h>
#include <sys/types.h> /* ssize_t */

/* Forward declarations: */
struct addrinfo;

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
 * @brief Enable address reuse on a socket.
 *
 * Sets the SO_REUSEADDR option on the provided socket file descriptor,
 * allowing the server to restart without waiting for old sockets to time out.
 *
 * @param socket_fd  Pointer to the socket file descriptor.
 * @retval  0  Success.
 * @retval -1 Failure (setsockopt failed).
 */
int socket_set_reusability(const int *socket_fd);

/**
 * @brief Enable fast restart on a socket by setting SO_LINGER.
 *
 * Configures the socket to discard unsent data and close immediately on shutdown,
 * which is suitable for listener sockets.
 *
 * @param socket_fd  Pointer to the socket file descriptor.
 * @retval  0  Success.
 * @retval -1 Failure (setsockopt failed).
 */
int socket_set_restartability(const int *socket_fd);

/**
 * @brief Set recommended socket options in the addrinfo hints structure.
 *
 * Initializes the provided hints structure for getaddrinfo() with recommended
 * values for dual-stack TCP listening sockets.
 *
 * @param hints  Pointer to the addrinfo structure to initialize.
 * @retval  0  Success.
 * @retval -1 Failure (invalid pointer).
 */
int socket_listener_set_hints(struct addrinfo *hints);

int64_t socket_drain(const int fd);

void socket_shutdown_and_close(int fd);

/**
 * @brief Initialize a listener socket with recommended options.
 *
 * Sets SO_REUSEADDR, SO_LINGER, and O_NONBLOCK on the provided socket, and
 * restricts IPv6 sockets to IPv6-only if required.
 *
 * @param listen_fd  File descriptor of the listener socket.
 * @param ai_family  Pointer to the address family (AF_INET/AF_INET6).
 * @retval  0  Success.
 * @retval -1 Failure (see log for details).
 */
int socket_listener_init(const int *listen_fd, const int32_t *ai_family);

/**
 * @brief Initialize a client socket with recommended options.
 *
 * Sets non-blocking mode and disables Nagle's algorithm for the client socket.
 *
 * @param client_fd  File descriptor of the client socket.
 * @retval  0  Success.
 * @retval -1 Failure (see log for details).
 */
int socket_client_init(const int *client_fd);


ssize_t socket_read_nonblocking(int fd, void *buf, size_t count);

#endif /* SERVER_SOCKET_HELPER_H */
