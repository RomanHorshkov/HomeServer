/**
 * @file reactor.h
 *
 * @brief Event reactor interface using epoll for I/O multiplexing.
 * Provides APIs to initialize, register, modify, and remove file descriptors, as well as to run the event loop dispatching callbacks on
 * readiness events.
 *
 * Designed for use in worker threads to handle multiple client connections in an event-driven manner.
 */

#ifndef SERVER_REACTOR_H
#define SERVER_REACTOR_H

/*****************************************************************************************************************************************
 * INCLUDES
 *****************************************************************************************************************************************
 */
#include <stddef.h>
#include <stdint.h>

#include "config_core.h"

/*****************************************************************************************************************************************
 * DEFINES
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * ENUMERATED TYPEDEFS
 *****************************************************************************************************************************************
 */

typedef struct fd_ctx fd_ctx_t;

typedef int (*fd_callback_fn)(int fd, fd_ctx_t* ctx);

/**
 * @brief File descriptor context structure.
 */
struct fd_ctx
{
    int            fd;      /* file descriptor */
    void*          owner;   /* listener_t*, worker_t*, connection_t* */
    fd_callback_fn handler; /* function to call on event */
    uint32_t       events;  /* current mask */
};

/*****************************************************************************************************************************************
 * STRUCTURED TYPEDEFS
 *****************************************************************************************************************************************
 */

/**
 * @struct reactor
 * @brief Internal state of the event reactor.
 *
 * Stores epoll instance and per-event dispatch information. Future extension includes tracking per-fd user context and callbacks.
 */

/* The reactor’s internal structure – opaque to users */
typedef struct
{
    /* reactor's epoll instance */
    int epoll_fd;

    /* epoll events for wait loop */
    struct epoll_event* events;

} reactor_t;

/*****************************************************************************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 *****************************************************************************************************************************************
 */

/**
 * @brief Initialize a reactor structure.
 *
 * Sets up the internal epoll instance and prepares the reactor to handle file descriptor event registration.
 *
 * @param reactor_ptr  Pointer to an allocated reactor_t structure.
 *
 * @retval  0  Success.
 * @retval -1  Failure to initialize (e.g., epoll_create failure).
 */
int reactor_init(reactor_t* reactor_ptr);

/**
 * @brief Register a file descriptor for read (EPOLLIN) events.
 *
 * Adds the specified file descriptor to the reactor's monitoring set, associating it with the provided user context and callback.
 *
 * @param reactor_ptr   Pointer to the reactor instance.
 * @param fd    File descriptor to monitor.
 * @param ctx   User-defined context associated with the fd.
 * @retval  0  Success.
 * @retval -1  Failure (e.g., epoll_ctl failure).
 */
int reactor_add_in(const reactor_t* reactor_ptr, const int fd, fd_ctx_t* ctx);

/**
 * @brief Register a client file descriptor for read (EPOLLIN) events
 *        with additional hangup/error monitoring.
 *
 * Similar to reactor_add_in(), but also monitors for peer shutdown and error conditions, suitable for client sockets.
 *
 * @param reactor_ptr   Pointer to the reactor instance.
 * @param fd    Client file descriptor to monitor.
 * @param ctx   User-defined context associated with the fd.
 * @retval  0  Success.
 * @retval -1  Failure (e.g., epoll_ctl failure).
 */
int reactor_add_in_client(const reactor_t* reactor_ptr, int fd, fd_ctx_t* ctx);

/**
 * @brief Register a file descriptor for write (EPOLLOUT) events.
 *
 * Adds the specified file descriptor to the reactor's monitoring set, associating it with the provided user context and callback.
 *
 * @param reactor_ptr   Pointer to the reactor instance.
 * @param fd    File descriptor to monitor.
 * @param ctx   User-defined context associated with the fd.
 * @retval  0  Success.
 * @retval -1  Failure (e.g., epoll_ctl failure).
 */
int reactor_add_out(const reactor_t* reactor_ptr, int fd, fd_ctx_t* ctx);

/**
 * @brief Modify the event mask and/or callback for a registered file descriptor.
 *
 * Allows updating the events and callback function associated with a descriptor already registered with the reactor.
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
int reactor_mod(const reactor_t* reactor_ptr, int fd, uint32_t events, fd_ctx_t* ctx);

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
int reactor_del(const reactor_t* reactor_ptr, int fd);

/**
 * @brief Start the reactor event loop.
 *
 * Enters a blocking loop on epoll_wait, dispatching events to the appropriate registered callbacks. Runs indefinitely until externally
 * stopped or interrupted.
 *
 * @param reactor_ptr  Pointer to the initialized reactor instance.
 */
int reactor_run(reactor_t* reactor_ptr, int* out_fd);

/**
 * @brief Clean up and deallocate a reactor instance.
 *
 * Closes the internal epoll file descriptor and frees the event buffer and reactor structure itself.
 *
 * @param reactor_ptr Pointer to the reactor instance.
 * @retval 0  Success.
 * @retval -1 Failure (invalid input).
 */
int reactor_shutdown(reactor_t* reactor_ptr);

#endif /* SERVER_REACTOR_H */
