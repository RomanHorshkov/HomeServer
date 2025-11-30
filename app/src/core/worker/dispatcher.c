#define _GNU_SOURCE

#include "worker/dispatcher.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <emlog.h>

#include "worker/operator.h"

#define LOG_TAG "srv_wrkr_dsptchr"

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

    /* TODO: consume listener pipeline and dispatch to operators */
    EML_INFO(LOG_TAG, "dispatcher thread started (placeholder loop)");

    for(;;)
    {
        /* Placeholder idle loop until dispatch logic is wired */
        sleep(1);
    }

    return NULL;
}
