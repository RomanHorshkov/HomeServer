
/**
 * @file pipeline.h
 * @brief
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025-05-15
 */

#ifndef SERVER_PIPELINE_H
#define SERVER_PIPELINE_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */
#include "config_core.h"

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC STRUCTURES
 ****************************************************************************
 */

typedef struct pipeline pipeline_t;

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief Initialize the pipeline structure for inter-thread communication.
 *
 * This function sets up the necessary file descriptors and SPSC ring buffer
 * for communication between the listener and worker threads.
 *
 * @param pipeline_ptr Pointer to a pipeline_t pointer that will be set to the
 *                     initialized pipeline instance.
 * @return STATUS_SUCCESS on success, STATUS_FAILURE on error.
 */
int pipeline_init(pipeline_t **pipeline_ptr_ptr);

int pipeline_push(const int client_fd);

int pipeline_pop(int *out_fd);

int pipeline_notify_worker_status_change(worker_status_t status);

int pipeline_get_wakeup_fd(void);

int pipeline_get_pipe_end_fd(int end);

void pipeline_destroy(void);

#endif /* SERVER_PIPELINE_H */
