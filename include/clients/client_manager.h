/**
 * @file client_manager.h
 * @brief Opaque connection‑tracking API used by the server core.
 *
 * A **client manager** owns bookkeeping information for every active
 * client connection handled by the parent process.  It keeps a mapping
 * `socket ⇆ child‑pid` (plus some optional peer metadata) and provides
 * helpers that the core loop calls when it:
 *   1. accepts a new socket,
 *   2. forks a child, or
 *   3. reaps a terminated child.
 *
 * The structure is opaque: callers manipulate it only through the
 * functions declared below.  All routines are *thread‑compatible* (they
 * share no hidden globals) but **not** automatically thread‑safe; add a
 * mutex inside the implementation if you move to a multi‑thread design.
 */

#ifndef SERVER_CLIENT_MANAGER_H
#define SERVER_CLIENT_MANAGER_H

#include <stddef.h>     /* size_t                       */
#include <sys/socket.h> /* struct sockaddr_storage      */
#include <sys/types.h>  /* pid_t                        */


/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
*/

/**
 * @struct client_manager
 * @brief Forward declaration of the opaque handle.
 *
 * The real definition lives in *client_manager.c*.
 */
struct client_manager;

/** Convenient typedef used throughout the code‑base. */
typedef struct client_manager client_manager_t;

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
*/

/*──────────────────────────────────────────────────────────────────────────*/
/*  Lifecycle                                                             */
/*──────────────────────────────────────────────────────────────────────────*/
/**
 * @brief Create an empty manager.
 *
 * @param clients_initial_amount
 *             Initial capacity hint (number of clients).  Use `0` for the
 *             default.  The manager will `realloc()` automatically if more
 *             space is required.
 *
 * @return Pointer to a freshly allocated structure on success, or
 *         `NULL` when allocation fails (`errno` is set to `ENOMEM`).
 *
 * @see client_manager_free()
 */
int client_manager_init(client_manager_t *client_manager);

/**
 * @brief Destroy a manager and release all its resources.
 *
 * Any client sockets still owned by the manager are closed first; then all
 * dynamic memory is freed.  Passing `NULL` is a no‑op.
 *
 * @param mgr  Handle obtained from client_manager_init().
 */
void client_manager_free(client_manager_t *mgr);

/*──────────────────────────────────────────────────────────────────────────*/
/*  Connection handling                                                    */
/*──────────────────────────────────────────────────────────────────────────*/
/**
 * @brief Register a newly accepted client socket.
 *
 * After a successful call the manager *owns* @p fd and will close it in
 * client_manager_free() or when the corresponding child terminates.
 *
 * @param mgr      Client manager handle.
 * @param addr     Peer address as returned by @c accept(2).
 * @param addrlen  Length of @p addr.
 * @param fd       Connected socket descriptor.
 *
 * @retval  0  Success.
 * @retval -1  Failure – `errno` is set.  In this case the caller still
 *             owns @p fd and must decide whether to close it.
 */
int client_manager_add_client(client_manager_t *mgr, const struct sockaddr_storage *addr,
                                socklen_t addrlen, int *file_descriptor);

/**
 * @brief Associate the PID of the forked child with its socket.
 *
 * Must be called in the **parent** immediately after a successful
 * `fork()`.  The manager uses the information to close the socket once the
 * child exits.
 *
 * @param mgr   Client manager.
 * @param child PID returned by @c fork().  Pass `0` if the fork failed –
 *              the function will then act as a no‑op.
 * @param file_descriptor    Socket previously passed to client_manager_add().
 */
void client_manager_set_pid(client_manager_t *mgr, pid_t child, int file_descriptor);

/**
 * @brief Handle a single connected client socket in the *child* process.
 *
 * The routine sets appropriate socket options, enters a blocking receive
 * loop, delegates request processing to the *browser* layer, and finally
 * closes the socket before returning.  It performs all logging necessary
 * for diagnostics.
 *
 * **Usage contract**
 *   * Must be invoked **only** in the forked child (never in the parent).
 *   * Takes ownership of @p socket_fd for the duration of the call.
 *   * Never throws; in the worst case it logs an error, closes the socket
 *     and returns.
 *
 * @param socket_fd  Connected client descriptor obtained from
 *                   @c accept(2).
 */
void client_manager_handle_socket(int socket_fd);

/**
 * @brief Remove bookkeeping for a child that has terminated.
 *
 * Should be invoked by the reap‑loop whenever `waitpid()` returns a PID
 * that belongs to one of the managed clients.  The underlying socket is
 * closed automatically.
 *
 * @param mgr  Client manager handle.
 * @param dead PID returned by @c waitpid().
 */
void client_manager_reap(client_manager_t *mgr, pid_t dead);


#endif /* SERVER_CLIENT_MANAGER_H */
