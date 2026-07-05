/**
 * @file worker.c
 * 
 * @brief Core worker logic and operator pool management.
 * The worker module manages a pool of operator threads, each responsible
 * for handling client connections. It provides functions to initialize
 * the worker pool, dispatch client connections to operators, and manage
 * operator lifecycles.
 * 
 * @note This module contains global state for the worker instance, because
 * it will be used, by design, by just a single thread per time, server core
 * process for initialization and listener thread at runtime.
 */


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE */

#include "worker.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "emlog.h"
#include "operator.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "srv_worker"

#define BLIND_ASSIGNMENT_LIMIT_PERCENTAGE (WORKER_MAX_CLIENTS)/(10U) /* 10% load */

/****************************************************************************
 * PRIVATE ENUMERATED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STRUCTURED TYPES
 ****************************************************************************
 */

typedef struct
{
    /**
     * @brief Current worker status
     */
    worker_status_t status;

    /**
     * @brief Array of operators managed by this worker
     */
    operator_t *operators;

    /**
     * @brief Threads for each operator
     */
    pthread_t* operators_threads;

    /**
     * @brief Number of operators in the pool
     */
    uint8_t operators_count;
} worker_t;

/****************************************************************************
 * PRIVATE VARIABLES DEFINITIONS
 ****************************************************************************
 */

worker_t _worker = {0};


/****************************************************************************
 * PRIVATE FUNCTION DECLARATIONS
 ****************************************************************************
 */

/**
 * @section balancer Lock-free operator selection strategy
 *
 * The dispatcher keeps a @c operator_t array. Each operator exposes
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

static uint8_t _compute_operator_count(uint8_t cpu_count);

/**
 * @brief Select the least-loaded operator using relaxed atomics.
 *
 * Implements a fast path for lightly loaded operators below the blind assignment limit.
 *
 * @param dispatcher Dispatcher instance.
 * @return Index of the selected operator.
 */
static operator_t* _least_loaded_operator(void);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
uint8_t worker_get_operators_count(void)
{
    return _worker.operators_count;
}

int worker_init(uint8_t cpu_count)
{
    _worker.operators_count = _compute_operator_count(cpu_count);
    if(_worker.operators_count == 0)
    {
        EML_PERR(LOG_TAG, "init: compute_operator_count failed");
        return STATUS_FAILURE;
    }

    /* Allocate an operator for each cpu available for operators */
    _worker.operators = calloc(_worker.operators_count, sizeof(operator_t));
    if(!_worker.operators)
    {
        EML_PERR(LOG_TAG, "init: operators alloc failed");
        return STATUS_FAILURE;
    }

    /* Allocate a thread for each operator */
    _worker.operators_threads = calloc(_worker.operators_count, sizeof(pthread_t));
    if(!_worker.operators_threads)
    {
        EML_PERR(LOG_TAG, "init: operators_threads alloc failed");
        goto fail;
    }

    /* Initialize each operator */
    for(uint8_t op_idx = 0; op_idx < _worker.operators_count; ++op_idx)
    {
        if(operator_init(&_worker.operators[op_idx], op_idx) != STATUS_SUCCESS)
        {
        EML_ERROR(LOG_TAG, "init: operator %u init failed", (unsigned)op_idx);
            goto fail;
        }

        /* Paranoia check */
        if(_worker.operators[op_idx].status != OPERATOR_STATUS_ACTIVE)
        {
            EML_ERROR(LOG_TAG, "init: operator %u not active, status %d", (unsigned)op_idx, _worker.operators[op_idx].status);
            goto fail;
        }
        
    }

    /* Set worker status to active */
    _worker.status = WORKER_STATUS_ACTIVE;



    EML_INFO(LOG_TAG, "Worker ready with %u operator%s (cpus=%u)",
             _worker.operators_count,
             (_worker.operators_count == 1) ? "" : "s",
             (unsigned)cpu_count);

    return STATUS_SUCCESS;

fail:
    worker_destroy();
    return STATUS_FAILURE;
}

int worker_dispatch_to_operator(int client_fd)
{
    if(client_fd < 0)
    {
        EML_ERROR(LOG_TAG, "_to_operator: invalid client fd");
        return STATUS_FAILURE;
    }

    if(_worker.status != WORKER_STATUS_ACTIVE)
    {
        EML_ERROR(LOG_TAG, "_to_operator: worker not active");
        return STATUS_FAILURE;
    }

    /* Get least loaded operator */
    operator_t *op = _least_loaded_operator();
    if(!op)
    {
        EML_ERROR(LOG_TAG, "_to_operator: failed to get least loaded operator");
        return STATUS_FAILURE;
    }

    /* Check operator's health */
    if (!op->ring || op->wakeup_ctx.fd == -1)
    {
        EML_ERROR(LOG_TAG, "_to_operator: least loaded operator not healthy");
        return STATUS_FAILURE;
    }

    /* Push new client's fd to the operator's ring */
    if(spsc_ring_push(op->ring, client_fd) != 0)
    {
        EML_ERROR(LOG_TAG, "_to_operator: failed pushing to operator's ring");
        return STATUS_FAILURE;
    }

    /* Signal to operator a new client's fd presence on the ring */    
    uint64_t wakeup_var = 1U;
    if(write(op->wakeup_ctx.fd, &wakeup_var, sizeof wakeup_var) != sizeof wakeup_var)
    {
        EML_PERR(LOG_TAG, "_to_operator: write to wakeup fd %d failed", op->wakeup_ctx.fd);
        /* continue anyway, operator will eventually notice the new fd */
    }

#ifdef DEBUG
    EML_DBG(LOG_TAG, "_to_operator: assigned client fd %d to operator %d",
             client_fd, op->id);
#endif
    return STATUS_SUCCESS;

}

int worker_run(void)
{
    /* run operators threads */
    for(uint8_t op_idx = 0; op_idx < _worker.operators_count; op_idx++)
    {
        if(pthread_create(&_worker.operators_threads[op_idx], NULL,
                          operator_thread, &_worker.operators[op_idx]) != 0)
        {
            EML_PERR(LOG_TAG, "operator %u thread creation failed",
                     (unsigned)op_idx);
            return STATUS_FAILURE;
        }
    }

    // /* join operator threads */
    // for(uint8_t op_idx = 0; op_idx < _worker.operators_count; op_idx++)
    // {
    //     if(pthread_join(_worker.operators_threads[op_idx], NULL) != 0)
    //     {
    //         EML_PERR(LOG_TAG, "operator %u thread join failed",
    //                  (unsigned)op_idx);
    //         return STATUS_FAILURE;
    //     }
    // }

    return STATUS_SUCCESS;
}

void worker_destroy(void)
{

    for(size_t i = 0; i < _worker.operators_count; ++i)
    {
        operator_shutdown(&_worker.operators[i]);
    }

    free(_worker.operators);
    _worker.operators = NULL;
    if(_worker.operators_threads)
    {
        free(_worker.operators_threads);
        _worker.operators_threads = NULL;
    }

    _worker.operators_threads = NULL;
    _worker.operators_count = 0;
}


/****************************************************************************
 * * PRIVATE FUNCTION DEFINITIONS
 ****************************************************************************
 */

static operator_t* _least_loaded_operator(void)
{
    uint8_t best_idx = 0;
    unsigned int best_load = WORKER_MAX_CLIENTS; /* start with max possible load */
    for(uint8_t i = 0; i < _worker.operators_count; ++i)
    {
        /* Skip full operators */
        if(_worker.operators[i].status == OPERATOR_STATUS_FULL) continue;

        unsigned int load = atomic_load_explicit(&_worker.operators[i].active_clients,
                                                    memory_order_relaxed);

        /* when load is below the blind assignment limit just give it to the operator */
        if(load < BLIND_ASSIGNMENT_LIMIT_PERCENTAGE)
        {
#ifdef DEBUG
            EML_DBG(LOG_TAG, "selecting operator %u with load=%u (blind)",
                     (unsigned)i, load);
#endif
            return &_worker.operators[i];
        }

        /* calculate min load over all operators */
        if(load < best_load)
        {
#ifdef DEBUG
            EML_DBG(LOG_TAG, "operator %u has new best load=%u",
                     (unsigned)i, load);
#endif
            best_load = load;
            best_idx = i;
        }
        /* skip overloaded operators, there are better around */

        /* Here can see if all the operators are FULL */
        if (i == _worker.operators_count - 1 && best_load == WORKER_MAX_CLIENTS)
        {
            /* All operators are full, set worker status to full! */
            _worker.status = WORKER_STATUS_FULL;
        }
        
    }

    if (best_load == WORKER_MAX_CLIENTS)
    {
        EML_ERROR(LOG_TAG, "all operators are at full capacity");
        return NULL;
    }

#ifdef DEBUG
    EML_DBG(LOG_TAG, "[dispatch] selecting operator %u with load=%u",
                (unsigned)best_idx, best_load);
#endif

    return &_worker.operators[best_idx];
}

static uint8_t _compute_operator_count(uint8_t cpu_count)
{
    uint8_t available_cpus = 0;
    /* Reserve one CPU for listener when possible */
    if(cpu_count <= 2)
    {
        available_cpus = 1;
    }

    /* give all cpus - 1 to the operators */
    else
    {
        available_cpus = cpu_count - 1;
    }

    EML_INFO(LOG_TAG, "Dispatcher sizing: cpu_count=%u, operators=%u",
             (unsigned)cpu_count, (unsigned)available_cpus);
    return available_cpus;
}
