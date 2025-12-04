/**
 * @file dispatcher.h
 * @brief Interface for the lock‑free worker dispatcher and operator pool.
 *
 * The dispatcher owns an array of worker operators. Each operator runs its
 * own thread and reactor. New client sockets are popped from a pipeline and
 * fanned out using a minimal O(N) load scan of @c active_count atomics.
 */
#ifndef SERVER_WORKER_DISPATCHER_H
#define SERVER_WORKER_DISPATCHER_H

#include <stdint.h>

#include "pipeline.h"
#include "operator.h"

/**
 * @brief Dispatcher state object.
 *
 * Encapsulates the operator pool and parameters derived from host CPU count.
 */
typedef struct worker_dispatcher
{
    pipeline_t *listener_pipeline; /**< Accepted client source pipeline */
    worker_operator_t *operators;  /**< Operator pool */
    size_t operator_count;         /**< Pool size */
    uint8_t cpu_count;             /**< Host CPU count */
    int db_initialized;            /**< db_app_init successfully completed */
} worker_dispatcher_t;

/**
 * @brief Initialize dispatcher and spawn operator threads.
 * @param dispatcher Target object.
 * @param listener_pipeline Pipeline from listener supplying accepted FDs.
 * @param cpu_count Detected logical CPU cores.
 * @return STATUS_SUCCESS on success; STATUS_FAILURE otherwise.
 */
int worker_dispatcher_init(worker_dispatcher_t *dispatcher,
                           pipeline_t *listener_pipeline,
                           uint8_t cpu_count);

/**
 * @brief Stop operators and release dispatcher resources.
 */
void worker_dispatcher_shutdown(worker_dispatcher_t *dispatcher);

/**
 * @brief Dispatcher thread entry point; expects a worker_dispatcher_t * as arg.
 */
void *worker_dispatcher_thread(void *arg);

#endif /* SERVER_WORKER_DISPATCHER_H */
