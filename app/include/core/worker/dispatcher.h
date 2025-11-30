#ifndef SERVER_WORKER_DISPATCHER_H
#define SERVER_WORKER_DISPATCHER_H

#include <stdint.h>

#include "pipeline.h"
#include "worker/operator.h"

typedef struct worker_dispatcher
{
    pipeline_t *listener_pipeline;
    worker_operator_t *operators;
    size_t operator_count;
    uint8_t cpu_count;
} worker_dispatcher_t;

int worker_dispatcher_init(worker_dispatcher_t *dispatcher,
                           pipeline_t *listener_pipeline,
                           uint8_t cpu_count);

void worker_dispatcher_shutdown(worker_dispatcher_t *dispatcher);

/**
 * @brief Dispatcher thread entry point; expects a worker_dispatcher_t * as arg.
 */
void *worker_dispatcher_thread(void *arg);

#endif /* SERVER_WORKER_DISPATCHER_H */
