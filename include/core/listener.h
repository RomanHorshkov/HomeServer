
/**
 * @file listener.h
 * @brief APIs for setting up, accepting, and tearing down server listener sockets.
 */
#ifndef SERVER_LISTENER_H
#define SERVER_LISTENER_H

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
*/

typedef struct listener listener_t;

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
*/

int listener_init(listener_t **listener_ptr, const char *port, int *pipe_write_fd);

void *listener_run(void *arg);

void listener_shutdown(listener_t **listener_ptr);



int set_socket_non_blocking(const int *socket_fd);

#endif /* SERVER_LISTENER_H */
