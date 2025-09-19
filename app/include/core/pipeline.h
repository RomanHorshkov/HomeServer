
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
#include "server_settings.h"

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

int pipeline_init(pipeline_t **pipeline_ptr_ptr);

int pipeline_push(pipeline_t *pipeline_ptr, const int client_fd);

int pipeline_pop(pipeline_t *pipeline_ptr);

int pipeline_notify_worker_status_change(pipeline_t   *pipeline,
                                         worker_status status);

int pipeline_get_wakeup_fd(pipeline_t *pipeline_ptr);

int pipeline_get_pipe_end_fd(pipeline_t *pipeline_ptr, int end);

void pipeline_destroy(pipeline_t **pipeline_ptr_ptr);

#endif /* SERVER_PIPELINE_H */
