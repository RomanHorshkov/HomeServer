/**
 * @file client.h
 * @brief Client socket handling API for operator threads.
 */
#ifndef SERVER_WORKER_CLIENT_H
#define SERVER_WORKER_CLIENT_H

#include <llhttp.h>
#include "server_settings.h"
#include "http_manager.h"

typedef struct worker_client_slot worker_client_slot_t;

/**
 * @brief Drain readable socket, advance HTTP parser, and decide lifecycle.
 * @return STATUS_SUCCESS to keep connection; STATUS_FAILURE to drop it.
 */
int client_handle(operator_t *op, worker_client_slot_t *slot);

#endif /* SERVER_WORKER_CLIENT_H */
