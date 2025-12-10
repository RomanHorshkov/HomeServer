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

#include "app_interface.h"
#include "operator.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "srv_wrkr_dsptchr"

#define BLIND_ASSIGNMENT_LIMIT_PERCENTAGE (WORKER_MAX_CLIENTS)/(10U) /* 10% load */


/****************************************************************************
 * PRIVATE FUNCTION DECLARATIONS
 ****************************************************************************
 */

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
    if(cpu_count <= 2)
    {
        EML_INFO(LOG_TAG, "low CPU count (%u); using single operator", (unsigned)cpu_count);
        return 1;
    }
    EML_INFO(LOG_TAG, "detected %u CPUs; using %u operators",
             (unsigned)cpu_count, (unsigned)(cpu_count - 2));
    return (uint8_t)(cpu_count - 2);
}

/**
 * @brief Select the least-loaded operator using relaxed atomics.
 *
 * Implements a fast path for lightly loaded operators below the blind assignment limit.
 *
 * @param dispatcher Dispatcher instance.
 * @return Index of the selected operator.
 */
static worker_operator_t* _least_loaded_operator(worker_dispatcher_t *dispatcher)
{

    uint8_t best_idx = 0;
    unsigned int best_load = WORKER_MAX_CLIENTS; /* start with max possible load */
    for(uint8_t i = 0; i < dispatcher->operator_count; ++i)
    {
        unsigned int load = atomic_load_explicit(&dispatcher->operators[i].active_count,
                                                    memory_order_relaxed);

        /* when load is below the blind assignment limit just give it to the operator */
        if(load < BLIND_ASSIGNMENT_LIMIT_PERCENTAGE)
        {
#ifdef DEBUG_MODE
            EML_DBG(LOG_TAG, "[dispatch] selecting operator %u with load=%u (blind)",
                     (unsigned)i, load);
#endif
            return &dispatcher->operators[i];
        }

        /* calculate min load over all operators */
        if(load < best_load)
        {
#ifdef DEBUG_MODE
            EML_DBG(LOG_TAG, "[dispatch] operator %u has new best load=%u",
                     (unsigned)i, load);
#endif
            best_load = load;
            best_idx = i;
        }
    }

    if (best_load == WORKER_MAX_CLIENTS)
    {
        EML_ERROR(LOG_TAG, "all operators are at full capacity");
        return NULL;
    }

    return &dispatcher->operators[best_idx];
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
    dispatcher->db_initialized = 0;

    /* Allocate an operator for each available cpu */
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
    }
    EML_INFO(LOG_TAG, "Initialized %zu operators", dispatcher->operator_count);

    if(db_app_init(dispatcher->operator_count) != 0)
    {
        EML_ERROR(LOG_TAG, "init: db_app_init failed");
        goto fail;
    }
    dispatcher->db_initialized = 1;

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
    if(dispatcher->db_initialized)
    {
        db_app_shutdown();
        dispatcher->db_initialized = 0;
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
        /* At each loop pop all the new client FDs from the pipeline (added by listener) */
        int client_fd = -1;
        while(pipeline_pop(&client_fd) == STATUS_SUCCESS)
        {
            /* Get least loaded operator */
            worker_operator_t *op = _least_loaded_operator(dispatcher);
            if (!op)
            {
                EML_ERROR(LOG_TAG, "failed to get least loaded operator");
                close(client_fd);
                continue;
            }

            /* Push client's fd to the operator's ring */
            spsc_ring_t *ring = worker_operator_get_ring(op);
            if(!ring || spsc_ring_push(ring, client_fd) != 0)
            {
                EML_ERROR(LOG_TAG, "failed getting operator ring or ring full, closing fd %d", client_fd);
                close(client_fd);
                continue;
            }

            /* Signal to operator a new client's fd presence on the ring */
            int wake_fd = worker_operator_get_wakeup_fd(op);
            if(write(wake_fd, &(uint64_t){1U}, sizeof(uint64_t)) != sizeof(uint64_t))
            {
                EML_PERR(LOG_TAG, "dispatcher: write to wakeup fd %d failed", wake_fd);
                /* continue anyway, operator will eventually notice the new fd */
            }

        }

        usleep(1000);
    }

    return NULL;
}
