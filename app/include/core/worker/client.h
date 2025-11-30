#ifndef SERVER_WORKER_CLIENT_H
#define SERVER_WORKER_CLIENT_H

#include "server_settings.h"

typedef struct worker_operator worker_operator_t;
typedef struct worker_client_slot worker_client_slot_t;

/**
 * @brief Handle a readable client socket.
 *
 * Returns STATUS_SUCCESS to keep the connection; STATUS_FAILURE signals the operator
 * to drop the client.
 */
int client_handle(worker_operator_t *op, worker_client_slot_t *slot);

#endif /* SERVER_WORKER_CLIENT_H */
