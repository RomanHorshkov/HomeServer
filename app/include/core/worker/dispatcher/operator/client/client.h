#ifndef SERVER_WORKER_CLIENT_H
#define SERVER_WORKER_CLIENT_H

#include <llhttp.h>
#include "server_settings.h"
#include "worker/dispatcher/operator/client/browser/http_manager.h"

typedef struct worker_operator worker_operator_t;
typedef struct worker_client_slot worker_client_slot_t;

typedef struct
{
    llhttp_t parser;
    llhttp_settings_t settings;
    char url[HTTP_MAX_PATH_LEN];
    http_method_t method;
    uint64_t body_bytes;
    int message_complete;
    /* header accumulation */
    char current_field[HTTP_MAX_HEADER_NAME_LEN];
    char current_value[HTTP_MAX_HEADER_VALUE_LEN];
    int in_header_field;
    HttpRequest req;
} client_http_state_t;

/**
 * @brief Handle a readable client socket.
 *
 * Returns STATUS_SUCCESS to keep the connection; STATUS_FAILURE signals the operator
 * to drop the client.
 */
int client_handle(worker_operator_t *op, worker_client_slot_t *slot);

int client_http_init(client_http_state_t *st);

#endif /* SERVER_WORKER_CLIENT_H */
