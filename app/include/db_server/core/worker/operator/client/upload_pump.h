/**
 * @file upload_pump.h
 * @brief The §9.4 streaming-upload seam: the DB_http stream gate and the socket-to-spool pump.
 *
 * Registered once per parser at operator boot; entered only when the gate claims a request (POST /api/app/files with an in-range
 * Content-Length). Everything else in the transport is untouched by uploads.
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    jul 2026
 * (c) 2026
 */
#ifndef DB_SERVER_CORE_WORKER_OPERATOR_CLIENT_UPLOAD_PUMP_H
#define DB_SERVER_CORE_WORKER_OPERATOR_CLIENT_UPLOAD_PUMP_H

/*****************************************************************************************************************************************
 * PUBLIC INCLUDES
 *****************************************************************************************************************************************
 */
#include <DB_http/DB_http.h>

#include <db_server/core/worker/operator/client/client.h>

/*****************************************************************************************************************************************
 * PUBLIC FUNCTIONS PROTOTYPES
 *****************************************************************************************************************************************
 */

/**
 * @brief The DB_http stream gate (DB_http_stream_gate_fn): claim POST /api/app/files when the declared size is within the
 *        configured upload cap. Oversize/malformed declarations are declined so the parser's own rejects answer them.
 */
int upload_stream_gate(uint8_t method, sv_t path, uint64_t content_length, void* ctx);

/**
 * @brief Run one claimed upload to completion: authorize (spool ticket) → pump body bytes socket→spool → commit → send the answer.
 *
 * The caller closes the connection afterwards regardless of outcome (uploads are one-shot); a response has been written on every
 * path except a peer that vanished mid-transfer.
 *
 * @param[in,out] cli        The claiming connection (parser paused at headers-complete).
 * @param[in]     thread_id  Operator thread id (the DB transaction slot).
 * @param[in]     parsed_req The headers-only DTO the parser returned with the claim.
 *
 * @return STATUS_SUCCESS when a response went out; STATUS_FAILURE when the peer died first.
 */
int client_upload_pump(client_t* cli, uint8_t thread_id, const DB_http_request_t* parsed_req);

#endif /* DB_SERVER_CORE_WORKER_OPERATOR_CLIENT_UPLOAD_PUMP_H */
