/**
 * @file core.h
 * @brief High‑level lifecycle API of the standalone HTTP server.
 *
 * The *core* module wires together the listener, the client‑manager and all
 * auxiliary utilities (logger, terminal control, …).  Its public contract
 * is intentionally minimal – only three calls – so that `main()` remains a
 * thin wrapper while unit‑tests or alternative front‑ends (e.g. a systemd
 * service) can embed the server with the same interface.
 *
 * ### Thread‑safety
 * Every function must be called from the same thread that created the
 * process.  The server uses `fork()` to isolate each client; it does not
 * employ threads internally.
 */

#ifndef SERVER_CORE_H
#define SERVER_CORE_H

#ifdef __cplusplus
extern "C"
{
#endif

    /****************************************************************************
     * PUBLIC DEFINES
     ****************************************************************************
     */
    /* None */

    /****************************************************************************
     * PUBLIC FUNCTIONS DECLARATIONS
     ****************************************************************************
     */

    /**
     * @brief Initialise all subsystems and start listening for connections.
     *
     * This **must** be the first call.  It performs the following steps:
     *   1. Opens/clears the log file and sets up logging.
     *   2. Creates one or more listening sockets (dual‑stack IPv4/IPv6 when
     *      available) on @p port.
     *   3. Allocates and initialises a fresh client‑manager instance.
     *
     * On success the server is ready – @ref server_run() will start accepting
     * clients.  On failure nothing is left allocated and it is safe for the
     * caller to terminate immediately.
     *
     * @param port  NUL‑terminated string with the TCP service ("80", "3490", …).
     *
     * @retval  0  Success.
     * @retval -1  One or more subsystems could not be initialised. `errno` is
     *             left untouched; consult the log for details.
     */
    int server_init(const char *port);

    /**
     * @brief Main accept‑fork loop.
     *
     * Blocks until the user types `'q'` + ENTER on the controlling terminal or
     * until a fatal internal error occurs.  The function:
     *   * calls `accept(2)` on each active listener,
     *   * forks a child; the child handles the socket and exits,
     *   * stores the `(socket, child‑pid)` tuple in the client manager,
     *   * reaps dead children and updates bookkeeping.
     *
     * The parent process never touches client sockets; it relies on the client
     * manager to close them once the associated child terminates.
     *
     * **Return value:** none.  The call exits only when a shutdown has been
     * requested; the caller should then invoke @ref server_shutdown().
     */
    void server_run(void);

    /**
     * @brief Gracefully shut down every subsystem.
     *
     * Closes listener sockets, frees the client‑manager, flushes/closes the log
     * file, and leaves the process in a clean state.  It is safe to call this
     * even if @ref server_init() returned `-1` – the function checks for
     * uninitialised pointers.
     */
    void server_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVER_CORE_H */