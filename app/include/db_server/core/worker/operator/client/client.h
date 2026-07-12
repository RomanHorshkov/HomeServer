/**
 * @file client.h
 * @brief Client socket handling API for operator threads.
 */
#ifndef SERVER_WORKER_CLIENT_H
#define SERVER_WORKER_CLIENT_H

#include <db_server/core/config_core.h>
#include <db_server/core/reactor.h> /* for fd_ctx_t */

#include <DB_http/DB_http.h>

/**
 * @brief Per‑client state owned by an operator thread.
 */
typedef struct
{
    uint8_t  is_busy;                       /**< slot in use */
    uint8_t  connection_policy;             /**< connection policy (keep alive / close) */
    uint64_t last_activity;                 /**< coarse ms timestamp of last I/O */
    size_t   request_count;                 /**< number of HTTP requests handled */

    fd_ctx_t ctx;                           /**< per‑fd reactor context */
    char     buf[DB_HTTP_MAX_BUFFER_LEN_B]; /**< buffer for socket reads */
    size_t   buf_idx;                       /**< cumulative bytes stored for current HTTP message */

    DB_http_request_t http_request;         /**< last parser DTO snapshot; views point into buf */
    DB_http_parser_t* http_parser;          /**< per-connection HTTP parser state */
} client_t;

/**
 * @brief Drain readable socket, advance HTTP parser, and decide lifecycle.
 *
 * @param cli Client containing parser state and buffers.
 * @param thread_id ID of the thread handling the client.
 *
 * @return STATUS_SUCCESS to keep connection; STATUS_FAILURE to drop it.
 */
int client_handle(client_t* cli, uint8_t thread_id);

/**
 * @brief Shutdown and cleanup client connection.
 * @return STATUS_SUCCESS on success, STATUS_FAILURE on error.
 */
void client_shutdown(client_t* cli);

/**
 * @brief Adopt an accepted fd onto @p cli for one connection (used by the upload worker pool).
 *
 * Centralises the fd/parser lifecycle in one place: clears the receive buffer, resets the request snapshot and the
 * per-message counters, resets the (reused) parser, and takes ownership of @p fd. The caller must have set
 * @c cli->http_parser to a live parser with its stream gate installed. After this returns the fd is @c cli->ctx.fd
 * and @c cli->is_busy is 1; drive it with @ref client_handle() then release with @ref client_release_fd().
 *
 * @param[in,out] cli Client to adopt onto (must carry a live @c http_parser).
 * @param[in]     fd  Accepted, non-blocking socket fd (ownership transfers to @p cli).
 */
void client_adopt_fd(client_t* cli, int fd);

/**
 * @brief Release the fd adopted by @ref client_adopt_fd(): shutdown+close it, clear the parser, drop busy state.
 *
 * Idempotent. Does NOT free @c cli->http_parser (the pool reuses it across connections); it only clears its state.
 *
 * @param[in,out] cli Client previously adopted onto.
 */
void client_release_fd(client_t* cli);

#endif /* SERVER_WORKER_CLIENT_H */
