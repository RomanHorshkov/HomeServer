#ifndef SERVER_WORKER_OPERATOR_H
#define SERVER_WORKER_OPERATOR_H

#include <pthread.h>
#include <stdint.h>

#include "config_core.h"
#include "spsc_ring.h"

typedef struct worker_operator worker_operator_t;

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
