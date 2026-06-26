/**
 * @file client.h
 * @brief Client socket handling API for operator threads.
 */
#ifndef SERVER_WORKER_CLIENT_H
#define SERVER_WORKER_CLIENT_H

#include "config_core.h"
#include "reactor.h" /* for fd_ctx_t */
#include "DB_http.h"

/**
 * @brief Per‑client state owned by an operator thread.
 */
typedef struct
{
    uint8_t         is_busy;                        /**< slot in use */
    uint8_t         connection_policy;              /**< connection policy (keep alive / close) */
    uint64_t        last_activity;                  /**< coarse ms timestamp of last I/O */
    size_t          request_count;                  /**< number of HTTP requests handled */

    fd_ctx_t        ctx;                            /**< per‑fd reactor context */
    char            buf[DB_HTTP_MAX_BUFFER_LEN_B];  /**< buffer for socket reads */
    size_t          buf_idx;                        /**< buffer written idx */
    
    // llhttp_parser_t http_parser_;                    /**< per-connection HTTP parser state */

    DB_http_request_t http_request;                  /**< per-connecton structured http request */
    DB_http_parser_t *http_parser;                   /**< per-connection HTTP parser state */
} client_t;

/**
 * @brief Drain readable socket, advance HTTP parser, and decide lifecycle.
 * 
 * @param cli Client containing parser state and buffers.
 * @param thread_id ID of the thread handling the client.
 *
 * @return STATUS_SUCCESS to keep connection; STATUS_FAILURE to drop it.
 */
int client_handle(client_t *cli, uint8_t thread_id);

/**
 * @brief Shutdown and cleanup client connection.
 * @return STATUS_SUCCESS on success, STATUS_FAILURE on error.
 */
void client_shutdown(client_t *cli);

#endif /* SERVER_WORKER_CLIENT_H */
