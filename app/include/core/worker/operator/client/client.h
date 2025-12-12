/**
 * @file client.h
 * @brief Client socket handling API for operator threads.
 */
#ifndef SERVER_WORKER_CLIENT_H
#define SERVER_WORKER_CLIENT_H

#include "config_core.h"
#include "reactor.h" /* for fd_ctx_t */
#include "http_manager.h"

typedef struct llhttp_parser_t; // believe me it exists

/**
 * @brief Per‑client state owned by an operator thread.
 */
typedef struct
{
    uint8_t is_busy;            /**< slot in use */
    fd_ctx_t ctx;               /**< per‑fd reactor context */
    uint32_t last_activity;     /**< coarse ms timestamp of last I/O */
    uint32_t request_count;     /**< number of HTTP requests handled */
    char recv_buf[HTTP_RECEIVE_BUFFER_LEN]; /**< buffer for socket reads */
    llhttp_parser_t http_parser;         /**< per-connection HTTP parser state */
} client_t;

/**
 * @brief Drain readable socket, advance HTTP parser, and decide lifecycle.
 * @return STATUS_SUCCESS to keep connection; STATUS_FAILURE to drop it.
 */
int client_handle(client_t *slot);

/**
 * @brief Shutdown and cleanup client connection.
 * @return STATUS_SUCCESS on success, STATUS_FAILURE on error.
 */
void client_shutdown(client_t *cli);

#endif /* SERVER_WORKER_CLIENT_H */
