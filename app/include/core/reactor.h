#ifndef SERVER_REACTOR_H
#define SERVER_REACTOR_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */
#include <stdint.h>
#include <stddef.h>

#include "config_core.h"

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC ENUMERATED VARIABLES
 ****************************************************************************
 */
// typedef enum {
//     FD_TYPE_NONE = 0,
//     FD_TYPE_LISTENER,
//     FD_TYPE_CLIENT,
//     FD_TYPE_PIPE,
//     FD_TYPE_EVENTFD,
//     FD_TYPE_TIMER,
//     FD_TYPE_TLS_WRAPPER,
// } fd_type_e;

typedef struct fd_ctx fd_ctx_t;

typedef int (*fd_callback_fn)(int fd, fd_ctx_t *ctx);

/**
 * @brief File descriptor context structure.
 */
struct fd_ctx
{
    int fd;                 /* file descriptor */
    void *owner;            /* listener_t*, worker_t*, connection_t* */
    fd_callback_fn handler; /* function to call on event */
    uint32_t events;        /* current mask */
};

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES
 ****************************************************************************
 */

/**
 * @struct reactor
 * @brief Internal state of the event reactor.
 *
 * Stores epoll instance and per-event dispatch information.
 * Future extension includes tracking per-fd user context and callbacks.
 */

/* The reactor’s internal structure – opaque to users */
typedef struct
{
    /* reactor's epoll instance */
    int epoll_fd;

    /* epoll events for wait loop */
    struct epoll_event *events;

} reactor_t;


/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************/

/**
 * @brief Initialize a reactor structure.
 *
 * Sets up the internal epoll instance and prepares the reactor
 * to handle file descriptor event registration.
 *
 * @param reactor_ptr  Pointer to an allocated reactor_t structure.
 * @param max_events   Maximum number of events.
 * @retval  0  Success.
 * @retval -1  Failure to initialize (e.g., epoll_create failure).
 */
int reactor_init(reactor_t *reactor_ptr, size_t max_events);

int reactor_add_in(const reactor_t *reactor_ptr, const int fd, fd_ctx_t *ctx);

int reactor_add_in_client(const reactor_t *reactor_ptr, int fd, fd_ctx_t *ctx);

int reactor_add_out(const reactor_t *reactor_ptr, int fd, fd_ctx_t *ctx);

/**
 * @brief Modify the event mask and/or callback for a registered file descriptor.
 *
 * Allows updating the events and callback function associated with a descriptor
 * already registered with the reactor.
 *
 * @note The file descriptor must have been added previously via reactor_add().
 *
 * @param reactor_ptr       Pointer to the reactor instance.
 * @param fd      File descriptor to modify.
 * @param ctx     Updated user-defined context (or same as before).
 * @param events  New event mask.
 * @retval  0     Success.
 * @retval -1    Failure (e.g., fd not found).
 */
int reactor_mod(const reactor_t *reactor_ptr, int fd, uint32_t events, fd_ctx_t *ctx);

/**
 * @brief Remove a file descriptor from the reactor.
 *
 * Stops monitoring the specified descriptor and removes its callback and context.
 *
 * @param reactor_ptr   Pointer to the reactor instance.
 * @param fd  File descriptor to remove.
 * @retval  0  Success.
 * @retval -1  Failure (e.g., fd not found).
 */
int reactor_del(const reactor_t *reactor_ptr, int fd);

/**
 * @brief Start the reactor event loop.
 *
 * Enters a blocking loop on epoll_wait, dispatching events to
 * the appropriate registered callbacks. Runs indefinitely until
 * externally stopped or interrupted.
 *
 * @param reactor_ptr  Pointer to the initialized reactor instance.
 */
int reactor_run(reactor_t *reactor_ptr, int *out_fd);

/**
 * @brief Clean up and deallocate a reactor instance.
 *
 * Closes the internal epoll file descriptor and frees the event buffer and
 * reactor structure itself.
 *
 * @param reactor_ptr Pointer to the reactor instance.
 * @retval 0  Success.
 * @retval -1 Failure (invalid input).
 */
int reactor_shutdown(reactor_t *reactor_ptr);

#endif /* SERVER_REACTOR_H */
