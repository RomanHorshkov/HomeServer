#ifndef SERVER_LISTENER_H
#define SERVER_LISTENER_H

#include <sys/socket.h>  // defines struct sockaddr_storage, socklen_t, accept(), etc.

// #ifdef __cplusplus
// extern "C" {
// #endif

/**
 * @file listener.h
 * @brief APIs for setting up, accepting, and tearing down server listener sockets.
 */

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
 */

typedef struct Listener Listener_t;

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

int listener_init(Listener_t **listener_ptr, const char *port);

int listener_check_incoming_clients(Listener_t **listener_ptr, struct sockaddr_storage *client_addr,
                                    socklen_t *client_addr_len, int *client_fd);

void listener_close(Listener_t **listener_ptr);

void listener_shutdown(Listener_t **listener_ptr);

// #ifdef __cplusplus
// }
// #endif

#endif /* SERVER_LISTENER_H */
