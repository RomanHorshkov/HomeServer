/**
 * @file core.h
 * @brief Core server management: initialization, run loop, shutdown.
 *
 * This header defines the main server struct and the core API for initializing, running, and shutting down the micro-HTTP server.
 *
 * The core manages the listener, worker, and control threads, as well as the pipe used for inter-thread communication.
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025-05-11
 */

#ifndef SERVER_CORE_H
#define SERVER_CORE_H

/*****************************************************************************************************************************************
 * PUBLIC DEFINES
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 *****************************************************************************************************************************************
 */

/**
 * @brief Initialise all core server subsystems and prepare for client connections.
 *
 * This function is the **mandatory first step** in starting the micro-HTTP server. It performs a comprehensive, multi-stage initialization
 * of all critical subsystems required for the server to operate. The function is designed to be robust and fail-safe: if any step fails, it
 * logs the error, aborts further initialization, and leaves the process in a safe state for immediate termination.
 *
 * **Initialization steps performed:**
 *
 *   1. **Pipe Creation:**
 *      - Establishes a unidirectional pipe for inter-thread communication between
 *        the listener (producer of new client sockets) and the worker (consumer).
 *      - Both ends of the pipe are set to non-blocking mode to prevent deadlocks
 *        and ensure responsiveness under high load or during shutdown.
 *
 *   2. **Logger Subsystem:**
 *      - Opens (or creates) the log file (`server.log`) and prepares the logging
 *        infrastructure for all subsequent operations.
 *      - Ensures that all errors, warnings, and informational messages are
 *        captured from the very beginning of the server's lifecycle.
 *
 *   3. **Listener Initialization:**
 *      - Allocates and configures the listener instance, which is responsible for
 *        accepting new incoming TCP connections on the specified @p port.
 *      - Supports dual-stack IPv4/IPv6 sockets when available, maximizing
 *        compatibility and reachability.
 *      - The listener is given the write end of the pipe to forward accepted
 *        client file descriptors to the worker.
 *
 *   4. **Worker Initialization:**
 *      - Allocates and configures the worker instance, which manages all active
 *        client sockets and handles their requests.
 *      - The worker is given the read end of the pipe to receive new client
 *        connections from the listener.
 *      - Prepares the worker for event-driven, scalable I/O using epoll.
 *
 *   5. **Final State:**
 *      - If all steps succeed, the server is fully initialized and ready to run.
 *      - The next step is to call @ref server_run(), which will start all threads
 *        and begin accepting and processing client connections.
 *      - If any step fails, the function logs the error and returns immediately.
 *        No resources are leaked, and it is safe for the caller to terminate.
 *
 * **Thread Safety:**
 * This function is not thread-safe and must be called exactly once, before any other server API. It must not be called concurrently with
 * @ref server_run().
 *
 * **Error Handling:**
 * On failure, the function logs a detailed error message for each failed subsystem. The global `errno` is left untouched; consult the log
 * file for diagnostics.
 *
 * @param api_spec     NUL-terminated API listen spec: a TCP service/port ("80", "3490") or a unix path
 *                     ("/run/home_server/api.sock"). API traffic is served by the operators.
 * @param upload_spec  NUL-terminated upload listen spec (a TCP port such as "3492" or a unix path), or NULL/""
 *                     to keep uploads on the operator path (no dedicated upload pool). Uploads accepted here run
 *                     on the isolated upload worker pool — see socket_rearchitecturing.md.
 *
 * @retval  0   Success. All subsystems initialized and ready.
 * @retval -1   One or more subsystems failed to initialize. No resources are
 *              left allocated; safe to terminate.
 *
 * @note This function must be called before @ref server_run() or any other
 *       server operation.
 *
 */
int server_init(const char* api_spec, const char* upload_spec);

/**
 * @brief Main server event loop: launches all core threads and blocks until shutdown.
 *
 * This function is the central orchestrator of the server’s runtime. It is responsible for launching and coordinating all major subsystems
 * as independent threads:
 *
 *   - **Listener Thread:** Accepts new incoming TCP connections on all configured sockets,
 *     and forwards accepted client file descriptors to the worker via a non-blocking pipe.
 *
 *   - **Worker Thread:** Receives new client sockets from the listener, manages all active
 *     client connections using an event-driven (epoll-based) loop, and dispatches requests
 *     to the appropriate handlers.
 *
 *   - **Control Thread:** Presents an interactive menu on the controlling terminal, allowing
 *     the operator to inspect server state (listeners, clients, etc.) and to initiate a
 *     graceful shutdown by typing `'q'` + ENTER.
 *
 * The function blocks until a shutdown is requested via the control thread (or until a fatal error causes a thread to exit). Upon shutdown,
 * all threads are joined to ensure a clean and orderly termination of all subsystems.
 *
 * **Threading Model:**
 * - Each subsystem runs in its own POSIX thread.
 * - Inter-thread communication (listener → worker) is performed via a non-blocking pipe.
 * - Shutdown is coordinated using atomic status flags in each subsystem.
 *
 * **Return value:** None. The function returns only after all threads have exited and
 * the server is ready for final cleanup (see @ref server_shutdown()).
 *
 * @note This function must be called exactly once, after successful @ref server_init().
 * @note The function is not reentrant or thread-safe.
 *
 */
void server_run(void);

#endif /* SERVER_CORE_H */
