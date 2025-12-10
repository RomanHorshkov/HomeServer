/**
 * @file worker.c
 * 
 * @brief Core worker logic and operator pool management.
 * The worker module manages a pool of operator threads, each responsible
 * for handling client connections. It provides functions to initialize
 * the worker pool, dispatch client connections to operators, and manage
 * operator lifecycles.
 */





#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE */

#include "worker.h"

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
    operator_t *operators;      /**< Operator pool */
    uint8_t operator_count;         /**< Pool size */
    // uint8_t cpu_count;             /**< Host CPU count */
    int db_initialized;            /**< db_app_init successfully completed */
} worker_t ;

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
int worker_init(uint8_t cpu_count)
{
    _worker.operator_count = _compute_operator_count(cpu_count);

    /* Allocate an operator for each available cpu */
    _worker.operators = calloc(_worker.operator_count, sizeof(operator_t));
    if(!_worker.operators)
    {
        EML_PERR(LOG_TAG, "init: operators alloc failed");
        return STATUS_FAILURE;
    }

    /* Initialize each operator */
    for(uint8_t op_idx = 0; op_idx < _worker.operator_count; ++op_idx)
    {
        if(operator_init(&_worker.operators[op_idx], op_idx) != STATUS_SUCCESS)
        {
            EML_ERROR(LOG_TAG, "init: operator %zu init failed", op_idx);
            goto fail;
        }
    }
    EML_INFO(LOG_TAG, "Initialized %zu operators", _worker.operator_count);

    if(db_app_init(_worker.operator_count) != 0)
    {
        EML_ERROR(LOG_TAG, "init: db_app_init failed");
        goto fail;
    }
    _worker.db_initialized = 1;

    EML_INFO(LOG_TAG, "Dispatcher ready with %zu operator%s (cpus=%u)",
             _worker.operator_count,
             (_worker.operator_count == 1) ? "" : "s",
             (unsigned)cpu_count);
    return _worker.operator_count;

fail:
    worker_destroy();
    return STATUS_FAILURE;
}

int worker_dispatch_to_operator(int client_fd)
{
    operator_t *op = NULL;
    uint8_t retry_max = 3;
    uint8_t retry_count = 0;
retry:
{
    if (retry_count++ >= retry_max)
    {
        EML_ERROR(LOG_TAG, "failed to dispatch client fd %d after %d retries",
                   client_fd, retry_count);
        return STATUS_FAILURE;
    }
    
    /* Get least loaded operator */
    op = _least_loaded_operator();
    if (!op)
    {
        EML_ERROR(LOG_TAG, "failed to get least loaded operator");
        goto retry;
    }

    /* Check operator's health */
    if (!op->ring || op->wakeup_fd == -1)
    {
        EML_ERROR(LOG_TAG, "least loaded operator not healthy");
        goto retry;
    }

    /* Push new client's fd to the operator's ring */
    if(spsc_ring_push(op->ring, client_fd) != 0)
    {
        EML_ERROR(LOG_TAG, "failed pushing to operator's ring or ring full");
        goto retry;
    }

    /* Signal to operator a new client's fd presence on the ring */    
    if(write(op->wakeup_fd, &(uint8_t){1U}, sizeof(uint8_t)) != sizeof(uint8_t))
    {
        EML_PERR(LOG_TAG, "dispatcher: write to wakeup fd %d failed", op->wakeup_fd);
        /* continue anyway, operator will eventually notice the new fd */
    }
}
    return STATUS_SUCCESS;
}

void worker_destroy(void)
{

    for(size_t i = 0; i < _worker.operator_count; ++i)
    {
        operator_shutdown(&_worker.operators[i]);
    }
    free(_worker.operators);
    _worker.operators = NULL;
    _worker.operator_count = 0;
    if(_worker.db_initialized)
    {
        db_app_shutdown();
        _worker.db_initialized = 0;
    }
}


/****************************************************************************
 * * PRIVATE FUNCTION DEFINITIONS
 ****************************************************************************
 */

static operator_t* _least_loaded_operator(void)
{
    uint8_t best_idx = 0;
    unsigned int best_load = WORKER_MAX_CLIENTS; /* start with max possible load */
    for(uint8_t i = 0; i < _worker.operator_count; ++i)
    {
        unsigned int load = atomic_load_explicit(&_worker.operators[i].active_count,
                                                    memory_order_relaxed);

        /* when load is below the blind assignment limit just give it to the operator */
        if(load < BLIND_ASSIGNMENT_LIMIT_PERCENTAGE)
        {
#ifdef DEBUG_MODE
            EML_DBG(LOG_TAG, "selecting operator %u with load=%u (blind)",
                     (unsigned)i, load);
#endif
            return &_worker.operators[i];
        }

        /* calculate min load over all operators */
        if(load < best_load)
        {
#ifdef DEBUG_MODE
            EML_DBG(LOG_TAG, "operator %u has new best load=%u",
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

#ifdef DEBUG_MODE
    EML_DBG(LOG_TAG, "[dispatch] selecting operator %u with load=%u",
                (unsigned)best_idx, best_load);
#endif

    return &_worker.operators[best_idx];
}

static uint8_t _compute_operator_count(uint8_t cpu_count)
{
    uint8_t available_cpus = 0;
    /* Reserve one CPU for listener and one for dispatcher when possible */
    if(cpu_count <= 2)
    {
        available_cpus = 1;
    }
    else
    {
        available_cpus = cpu_count - 2;
    }

    EML_INFO(LOG_TAG, "Dispatcher sizing: cpu_count=%u, operators=%u",
             (unsigned)cpu_count, (unsigned)available_cpus);
    return available_cpus;
}
