/**
 * @file dispatcher.c
 * @brief Worker dispatcher thread and lock-free operator load balancer.
 *
 * The dispatcher fans incoming client sockets out to a pool of worker
 * operators. Each operator owns its own thread, epoll/reactor instance,
 * a mailbox ring, and a wakeup eventfd.
 *
 * This module contains a lock-free, contention-minimising load balancer
 * that picks the destination operator for each accepted client based on
 * the current number of active clients per operator.
 */

#define _GNU_SOURCE

#include "worker/dispatcher/dispatcher.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>
#include <limits.h>

#include <emlog.h>

#include "worker/dispatcher/operator/operator.h"

#define LOG_TAG "srv_wrkr_dsptchr"

/**
 * @section balancer Lock-free operator selection strategy
 *
 * The dispatcher keeps a @c worker_operator_t array. Each operator exposes
 * an @c atomic_uint @c active_count that is updated by the operator thread
 * whenever clients are added/removed. The dispatcher thread chooses the
 * "least loaded" operator by scanning the array and reading these counters
 * with @c memory_order_relaxed.
 *
 * Rationale and properties:
 * - No locks: selection uses only atomic loads, avoiding mutex contention
 *   between the dispatcher and operator threads.
 * - Eventually correct: counters may be slightly stale while an operator is
 *   in the middle of accepting/removing a client, but the race only affects
 *   a single assignment and quickly converges as counts are updated.
 * - Fast path for idle operators: if any operator reports zero load, it is
 *   immediately selected. This reduces latency during ramp-ups and helps
 *   cold operators receive their first connection quickly.
 * - Fairness: a linear scan taking the minimum produces near‑uniform
 *   distribution under steady load for small operator counts (tens). For
 *   very large pools the scan cost is still tiny compared to I/O, but a
 *   heap or power-of-two-choices scheme could be substituted later.
 * - Memory ordering: @c memory_order_relaxed is sufficient here because
 *   the only contract is to read a quasi‑current load number; there is no
 *   dependence on happens‑before relationships with the operator’s internal
 *   data structures.
 *
 * Complexity:
 * - Selection is O(N) in the number of operators per connection dispatch.
 *
 * Failure handling:
 * - If the destination operator’s ring is full (should be rare), the
 *   dispatcher closes the client FD and logs an error rather than blocking.
 */

static size_t _compute_operator_count(uint8_t cpu_count)
{
    /* Reserve one CPU for listener and one for dispatcher when possible */
    if(cpu_count <= 1)
    {
        return 1;
    }
    if(cpu_count <= 2)
    {
        return 1;
    }
    return (size_t)(cpu_count - 2);
}

int worker_dispatcher_init(worker_dispatcher_t *dispatcher,
                           pipeline_t *listener_pipeline,
                           uint8_t cpu_count)
{
    if(dispatcher == NULL || listener_pipeline == NULL)
    {
        EML_ERROR(LOG_TAG, "init: invalid input");
        return STATUS_FAILURE;
    }

    memset(dispatcher, 0, sizeof(*dispatcher));
    dispatcher->listener_pipeline = listener_pipeline;
    dispatcher->cpu_count = cpu_count;
    dispatcher->operator_count = _compute_operator_count(cpu_count);

    dispatcher->operators = calloc(dispatcher->operator_count, sizeof(worker_operator_t));
    if(!dispatcher->operators)
    {
        EML_PERR(LOG_TAG, "init: operators alloc failed");
        return STATUS_FAILURE;
    }

    for(size_t i = 0; i < dispatcher->operator_count; ++i)
    {
        if(worker_operator_init(&dispatcher->operators[i], (int)i) != STATUS_SUCCESS)
        {
            EML_ERROR(LOG_TAG, "init: operator %zu init failed", i);
            goto fail;
        }
        if(worker_operator_start(&dispatcher->operators[i]) != STATUS_SUCCESS)
        {
            EML_ERROR(LOG_TAG, "init: operator %zu start failed", i);
            goto fail;
        }
    }

    EML_INFO(LOG_TAG, "Dispatcher ready with %zu operator%s (cpus=%u)",
             dispatcher->operator_count,
             (dispatcher->operator_count == 1) ? "" : "s",
             (unsigned)cpu_count);
    return STATUS_SUCCESS;

fail:
    worker_dispatcher_shutdown(dispatcher);
    return STATUS_FAILURE;
}

void worker_dispatcher_shutdown(worker_dispatcher_t *dispatcher)
{
    if(!dispatcher)
    {
        return;
    }

    if(dispatcher->operators)
    {
        for(size_t i = 0; i < dispatcher->operator_count; ++i)
        {
            worker_operator_shutdown(&dispatcher->operators[i]);
        }
        free(dispatcher->operators);
        dispatcher->operators = NULL;
    }
}

void *worker_dispatcher_thread(void *arg)
{
    worker_dispatcher_t *dispatcher = (worker_dispatcher_t *)arg;
    if(!dispatcher)
    {
        EML_ERROR(LOG_TAG, "thread: invalid dispatcher");
        return NULL;
    }

    EML_INFO(LOG_TAG, "dispatcher thread started");

    for(;;)
    {
        int client_fd = -1;
        while(pipeline_pop(&client_fd) == STATUS_SUCCESS)
        {
            /*
             * Pick operator with zero load if any, otherwise minimum load.
             * This is a lock‑free, approximate decision based on relaxed
             * atomic reads of each operator's active_count.
             */
            size_t best_idx = 0;
            unsigned int best_load = UINT_MAX;
            for(size_t i = 0; i < dispatcher->operator_count; ++i)
            {
                unsigned int load = atomic_load_explicit(&dispatcher->operators[i].active_count,
                                                         memory_order_relaxed);
                if(load == 0)
                {
                    best_idx = i;
                    best_load = 0;
                    break;
                }
                if(load < best_load)
                {
                    best_load = load;
                    best_idx = i;
                }
            }

            worker_operator_t *op = &dispatcher->operators[best_idx];

#ifdef DEBUG_MODE
            EML_DBG(LOG_TAG, "[dispatch] fd %d -> op %zu (load=%u)", client_fd, best_idx, best_load);
#endif

            spsc_ring_t *ring = worker_operator_get_ring(op);
            if(!ring || spsc_ring_push(ring, client_fd) != 0)
            {
                EML_ERROR(LOG_TAG, "failed to enqueue fd %d to operator %zu", client_fd, best_idx);
                close(client_fd);
                continue;
            }

            int wake_fd = worker_operator_get_wakeup_fd(op);
            if(write(wake_fd, &(uint64_t){1U}, sizeof(uint64_t)) != sizeof(uint64_t))
            {
                EML_PERR(LOG_TAG, "failed to wake operator %zu", best_idx);
            }
        }

        usleep(1000);
    }

    return NULL;
}
