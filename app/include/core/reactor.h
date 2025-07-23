#ifndef SERVER_REACTOR_H
#define SERVER_REACTOR_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */
#include <stdint.h>

#include "server_settings.h"

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC ENUMERATED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DEFINITIONS
 ****************************************************************************
 */
typedef struct reactor reactor_t;

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************/

typedef int (*reactor_callback)(int fd, uint32_t events, void *ctx);

/**
 * @brief Initialize a reactor structure.
 *
 * Sets up the internal epoll instance and prepares the reactor
 * to handle file descriptor event registration.
 *
 * @param r  Pointer to an allocated reactor_t structure.
 * @retval  0  Success.
 * @retval -1  Failure to initialize (e.g., epoll_create failure).
 */
int reactor_init(reactor_t **reactor);

int reactor_add_in(reactor_t *reactor, int fd, reactor_callback cb, void *ctx);

int reactor_add_in_client(reactor_t *reactor, int fd, reactor_callback cb, void *ctx);

int reactor_add_out(reactor_t *reactor, int fd, reactor_callback cb, void *ctx);

/**
 * @brief Modify the event mask and/or callback for a registered file descriptor.
 *
 * Allows updating the events and callback function associated with a descriptor
 * already registered with the reactor.
 *
 * @note The file descriptor must have been added previously via reactor_add().
 *
 * @param r       Pointer to the reactor instance.
 * @param fd      File descriptor to modify.
 * @param ctx     Updated user-defined context (or same as before).
 * @param cb      Updated callback function.
 * @param events  New event mask.
 * @retval  0     Success.
 * @retval -1    Failure (e.g., fd not found).
 */
int reactor_mod(reactor_t *reactor, int fd, uint32_t events, reactor_callback cb, void *ctx);

/**
 * @brief Remove a file descriptor from the reactor.
 *
 * Stops monitoring the specified descriptor and removes its callback and context.
 *
 * @param r   Pointer to the reactor instance.
 * @param fd  File descriptor to remove.
 * @retval  0  Success.
 * @retval -1  Failure (e.g., fd not found).
 */
int reactor_del(reactor_t *reactor, int fd);

/**
 * @brief Start the reactor event loop.
 *
 * Enters a blocking loop on epoll_wait, dispatching events to
 * the appropriate registered callbacks. Runs indefinitely until
 * externally stopped or interrupted.
 *
 * @param r  Pointer to the initialized reactor instance.
 */
int reactor_run(reactor_t *reactor, int *out_fd);

#endif /* SERVER_REACTOR_H */
