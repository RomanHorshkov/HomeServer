#ifndef SERVER_WORKER_OPERATOR_H
#define SERVER_WORKER_OPERATOR_H

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>

#include "config_core.h"
#include "spsc_ring.h"
#include "reactor.h"
#include "worker/client.h"

typedef struct worker_operator worker_operator_t;
typedef struct fd_ctx_s fd_ctx_t;

struct worker_client_slot
{
    int fd;                     /* client socket fd */
    fd_ctx_t *ctx;              /* fd context */
    uint32_t last_activity;     /* last activity timestamp */
    uint32_t request_count;     /* number of requests handled */
};

struct worker_operator
{
    int id;
    pthread_t thread;

    /* Mailbox: dispatcher -> operator */
    spsc_ring_t *ring;
    int wakeup_fd;
    fd_ctx_t *wakeup_ctx;

    /* Event core */
    reactor_t reactor;
    int timer_fd;
    uint32_t timer_frequency;

    /* Clients */
    worker_client_slot_t clients[WORKER_MAX_CLIENTS];
    size_t active_clients;

    /* Status */
    worker_status_t status;
};

int worker_operator_init(worker_operator_t *op, int id);

int worker_operator_start(worker_operator_t *op);

void worker_operator_shutdown(worker_operator_t *op);

/**
 * @brief Operator thread entry point; expects a worker_operator_t * as arg.
 */
void *worker_operator_thread(void *arg);

/**
 * @brief Access mailbox components for dispatcher -> operator handoff.
 */
int worker_operator_get_wakeup_fd(const worker_operator_t *op);
spsc_ring_t *worker_operator_get_ring(worker_operator_t *op);

#endif /* SERVER_WORKER_OPERATOR_H */
