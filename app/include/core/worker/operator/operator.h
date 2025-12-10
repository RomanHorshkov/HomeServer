/**
 * @file operator.h
 * @brief Operator thread interface: per‑thread reactor, mailbox, clients.
 */
#ifndef SERVER_WORKER_OPERATOR_H
#define SERVER_WORKER_OPERATOR_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include "config_core.h"
#include "reactor.h"
#include "spsc_ring.h"
#include "client.h"
#include "http_manager.h"

typedef struct fd_ctx_s fd_ctx_t;

/**
 * @brief Per‑client state owned by an operator thread.
 */
typedef struct
{
    int fd;                     /**< client socket fd */
    fd_ctx_t *ctx;              /**< per‑fd reactor context */
    uint32_t last_activity;     /**< coarse ms timestamp of last I/O */
    uint32_t request_count;     /**< number of HTTP requests handled */
    char recv_buf[HTTP_RECEIVE_BUFFER_LEN]; /**< staging buffer for socket reads */
    http_parser_t http;         /**< per-connection HTTP parser state */
} worker_client_slot_t;

/**
 * @brief Operator thread state.
 */
typedef struct
{
    uint8_t id;                 /**< stable operator id (for logs/metrics) */
    pthread_t thread;           /**< operator thread handle */

    /* Mailbox: dispatcher -> operator */
    spsc_ring_t *ring;          /**< SPSC ring carrying new client FDs */
    int wakeup_fd;              /**< eventfd to wake the reactor on new mail */
    fd_ctx_t *wakeup_ctx;       /**< reactor context for wakeup fd */

    /* Event core */
    reactor_t reactor;          /**< epoll/reactor instance */
    int timer_fd;               /**< timerfd for housekeeping */
    uint32_t timer_frequency;   /**< current timer cadence (sec) */

    /* Clients */
    worker_client_slot_t clients[WORKER_MAX_CLIENTS]; /**< slots */
    size_t active_clients;      /**< number of in‑use slots */
    atomic_uint active_count;   /**< visible to worker for load balancing */

    /* Status */
    worker_status_t status;     /**< lifecycle state */
} operator_t;

/**
 * @brief Initialize operator state (ring, wakeup, counters).
 * @param op Operator object to initialize.
 * @param id Stable operator identifier for logs/metrics.
 * @return STATUS_SUCCESS on success, STATUS_FAILURE on error.
 */
int operator_init(operator_t *op, uint8_t id);

/** Stop operator thread and release resources. */
void operator_shutdown(operator_t *op);

/**
 * @brief Operator thread entry point; expects a operator_t * as arg.
 */
void *operator_thread(void *arg);

/**
 * @brief Access mailbox components for dispatcher -> operator handoff.
 */
/** eventfd used by dispatcher to wake the operator. */
int operator_get_wakeup_fd(const operator_t *op);
/** mailbox ring used to enqueue new client FDs. */
spsc_ring_t *operator_get_ring(operator_t *op);

#endif /* SERVER_WORKER_OPERATOR_H */
