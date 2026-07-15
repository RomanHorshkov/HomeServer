/**
 * @file operator.h
 * @brief Operator thread interface: per‑thread reactor, mailbox, clients.
 */
#ifndef SERVER_WORKER_OPERATOR_H
#define SERVER_WORKER_OPERATOR_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include <db_server/core/config_core.h>
#include <db_server/core/reactor.h>
#include <db_server/core/worker/operator/client/client.h>
#include <spscring.h>

/*****************************************************************************************************************************************
 * PUBLIC DEFINES
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * PUBLIC STRUCTURED VARIABLES DEFINITIONS
 *****************************************************************************************************************************************
 */

/**
 * @brief Operator status.
 * Used to track the lifecycle state of an operator thread.
 *
 * good: ACTIVE, FULL, min 1 bit set in the low bits (0x0F) bad: SHUTDOWN, INVALID, min 1 bit set in the high bits (0xF0)
 */
typedef enum
{
    /**
     * @brief Uninitialized: not yet started.
     */
    OPERATOR_STATUS_UNINITIALIZED = 0,

    /**
     * @brief Active: ready to receive new clients.
     */
    OPERATOR_STATUS_ACTIVE = 1 << 0,

    /**
     * @brief Full: no more clients can be accepted.
     */
    OPERATOR_STATUS_FULL = 1 << 1,

    /**
     * @brief Shutdown: shutting down and cleaning up.
     */
    OPERATOR_STATUS_SHUTDOWN = 1 << 6,

    /**
     * @brief Invalid: max value for operator status.
     */
    OPERATOR_STATUS_INVALID = 1 << 7,
} operator_status_t;

/**
 * @brief Operator thread state.
 */
typedef struct
{
    /**
     * @brief Lifecycle status of this operator.
     *
     * _Atomic: written by the worker/dispatch thread (ACTIVE/FULL) and by the
     * shutdown path (SHUTDOWN), and read in the operator thread's run loop — so
     * every access must be atomic (C11 makes plain load/store on an _Atomic
     * object atomic).
     */
    _Atomic operator_status_t status;

    /**
     * @brief Stable operator identifier.
     * Used as thread no and for logs/metrics.
     */
    uint8_t id;

    /**
     * @brief SPSC ring used to receive new clients fds.
     */
    spsc_ring_t* ring;

    /**
     * @brief Wakeup reactor context.
     * Used by dispatcher to wake the operator thread.
     */
    fd_ctx_t wakeup_ctx;

    /**
     * @brief Timer fd for periodic housekeeping.
     */
    int timer_fd;

    /**
     * @brief Timer housekeeping frequency (seconds).
     */
    uint32_t timer_period;

    /**
     * @brief Operator's reactor instance (epoll).
     */
    reactor_t reactor;

    /**
     * @brief Client slots owned by this operator.
     */
    client_t clients[WORKER_MAX_CLIENTS];

    /**
     * @brief Active clients count.
     * This is the number of clients currently being handled by this operator. atomic because visible to worker for load balancing.
     */
    atomic_uint active_clients;

    /**
     * @brief Queued-but-not-yet-dequeued clients count.
     * Incremented by the dispatcher on a successful spsc_ring_push, decremented by this operator's
     * own thread on spsc_ring_pop — combined with active_clients as the dispatcher's effective_load,
     * so an operator whose ring is full is never mistaken for idle (active_clients alone doesn't see
     * connections still sitting in the ring). atomic for the same load-balancing-visibility reason.
     */
    atomic_uint queued_clients;

} operator_t;

/*****************************************************************************************************************************************
 * PUBLIC FUNCTION DECLARATIONS
 *****************************************************************************************************************************************
 */

/**
 * @brief Initialize an operator instance.
 *
 * This function initializes the operator instance, setting up the necessary resources such as the reactor, mailbox, and client slots.
 *
 * @param op            Pointer to operator_t structure to initialize.
 * @param id             Stable operator identifier.
 * @param ring_capacity Mailbox (SPSC ring) capacity — resolved once at worker_init() time
 *                       (see worker.c's DB_SERVER_RING_CAPACITY handling), not a per-operator
 *                       choice; every operator gets the same value. Must be a power of two.
 * @return STATUS_SUCCESS on success, STATUS_FAILURE on error.
 */
int operator_init(operator_t* op, uint8_t id, uint32_t ring_capacity);

/**
 * @brief Ask a RUNNING operator thread to stop: set SHUTDOWN atomically and wake
 *        it out of epoll. Does NOT free anything — the caller must pthread_join
 *        the thread and only then call operator_shutdown(). Safe to call from
 *        another thread.
 * @param op Operator object to signal.
 */
void operator_request_shutdown(operator_t* op);

/**
 * @brief Destroy an operator's resources (ring, fds, clients, reactor). Must be
 *        called ONLY when no operator thread is running on @p op — after a
 *        pthread_join following operator_request_shutdown(), or on an operator
 *        whose thread never started.
 * @param op Operator object to shutdown.
 */
void operator_shutdown(operator_t* op);

/**
 * @brief Operator thread entry point; expects a operator_t * as arg.
 */
void* operator_thread(void* arg);

#endif /* SERVER_WORKER_OPERATOR_H */
