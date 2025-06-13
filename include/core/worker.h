/**
 * @file worker.h
 * @brief APIs for setting up, accepting, and tearing down server client sockets.
 */
#ifndef SERVER_WORKER_H
#define SERVER_WORKER_H

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
*/

typedef struct worker worker_t;

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
*/

int worker_init(worker_t **worker_ptr, int *pipe_read_fd);

void *worker_run(void *arg);

#endif /* SERVER_WORKER_H */
