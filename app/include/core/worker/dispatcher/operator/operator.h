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

typedef struct worker_operator worker_operator_t;
typedef struct fd_ctx_s fd_ctx_t;

/**
 * @brief Per‑client state owned by an operator thread.
 */
struct worker_client_slot
{
    int fd;                     /**< client socket fd */
    fd_ctx_t *ctx;              /**< per‑fd reactor context */
    uint32_t last_activity;     /**< coarse ms timestamp of last I/O */
    uint32_t request_count;     /**< number of HTTP requests handled */
    char recv_buf[HTTP_RECEIVE_BUFFER_LEN]; /**< staging buffer for socket reads */
    http_parser_t http;         /**< per-connection HTTP parser state */
};

/**
 * @brief Operator thread state.
 */
struct worker_operator
{
    int id;                     /**< stable operator id (for logs/metrics) */
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
    atomic_uint active_count;   /**< visible to dispatcher for load balancing */

    /* Status */
    worker_status_t status;     /**< lifecycle state */
};

/** Initialize operator internals (ring, eventfd, counters). */
int worker_operator_init(worker_operator_t *op, int id);

/** Spawn operator thread. */
int worker_operator_start(worker_operator_t *op);

/** Stop operator thread and release resources. */
void worker_operator_shutdown(worker_operator_t *op);

/**
 * @brief Operator thread entry point; expects a worker_operator_t * as arg.
 */
void *worker_operator_thread(void *arg);

/**
 * @brief Access mailbox components for dispatcher -> operator handoff.
 */
/** eventfd used by dispatcher to wake the operator. */
int worker_operator_get_wakeup_fd(const worker_operator_t *op);
/** mailbox ring used to enqueue new client FDs. */
spsc_ring_t *worker_operator_get_ring(worker_operator_t *op);

#endif /* SERVER_WORKER_OPERATOR_H */
