#define _GNU_SOURCE

#include "worker/worker.h"

#include <stdlib.h>

#include <emlog.h>

#include "worker/dispatcher/dispatcher.h"

#define LOG_TAG "srv_worker"

struct worker
{
    worker_dispatcher_t *dispatcher;
};

int worker_init(worker_t **worker_ptr_ptr, pipeline_t *pipeline_ptr, uint8_t available_cpu_count)
{
    /* Validate inputs */
    if(worker_ptr_ptr == NULL || pipeline_ptr == NULL)
    {
        EML_ERROR(LOG_TAG, "worker_init: invalid input");
        return STATUS_FAILURE;
    }

    worker_t *worker_ptr = calloc(1, sizeof(*worker_ptr));
    if(!worker_ptr)
    {
        EML_PERR(LOG_TAG, "worker_init: calloc failed");
        return STATUS_FAILURE;
    }

    worker_ptr->dispatcher = calloc(1, sizeof(*worker_ptr->dispatcher));
    if(!worker_ptr->dispatcher)
    {
        EML_PERR(LOG_TAG, "worker_init: calloc dispatcher failed");
        free(worker_ptr);
        return STATUS_FAILURE;
    }

    if(worker_dispatcher_init(worker_ptr->dispatcher, pipeline_ptr, available_cpu_count) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "worker_init: dispatcher init failed");
        free(worker_ptr->dispatcher);
        free(worker_ptr);
        return STATUS_FAILURE;
    }

    *worker_ptr_ptr = worker_ptr;
    return STATUS_SUCCESS;
}

void *worker_run(void *arg)
{
    if(arg == NULL)
    {
        EML_ERROR(LOG_TAG, "worker_run: invalid input");
        return NULL;
    }

    worker_t *worker_ptr = (worker_t *)arg;
    return worker_dispatcher_thread(worker_ptr->dispatcher);
}

void worker_destroy(worker_t *worker_ptr)
{
    if(!worker_ptr)
    {
        return;
    }

    if(worker_ptr->dispatcher)
    {
        worker_dispatcher_shutdown(worker_ptr->dispatcher);
        free(worker_ptr->dispatcher);
    }
    free(worker_ptr);
}
