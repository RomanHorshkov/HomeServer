/**
 * @file response_writer.h
 *
 * @brief Serialize a DB_app response into the client buffer and send it (§9.2).
 *
 * The client's single 32 KiB receive buffer is reused for the response — legal because DB_http rejects pipelined/trailing bytes and chunked
 * bodies, so once `db_app_run()` returned there is never unread request data behind the parsed message, and the request views are dead by
 * the §8.2 contract.
 *
 * A response that does not fit the buffer is NEVER truncated: the writer logs loudly and sends a minimal owned `500
 * {"error":"server_error"}`.
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    jul 2026
 * (c) 2026
 */
#ifndef SERVER_WORKER_CLIENT_RESPONSE_WRITER_H
#define SERVER_WORKER_CLIENT_RESPONSE_WRITER_H

/*****************************************************************************************************************************************
 * INCLUDES
 *****************************************************************************************************************************************
 */
#include <db_app/response/response.h>

#include <db_server/core/worker/operator/client/client.h>

/*****************************************************************************************************************************************
 * DEFINES
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * ENUMERATED TYPEDEFS
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * ENUMERATED VARIABLES
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * STRUCTURED TYPEDEFS
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * STRUCTURED VARIABLES
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * FUNCTIONS DECLARATIONS
 *****************************************************************************************************************************************
 */

/**
 * @brief Serialize @p res into the client buffer and send it completely.
 *
 * Steps 6-8 of the §9.2 sequence: status line → Content-Type / Content-Length / Connection (from the client's parsed policy) → every extra
 * header on its own line → CRLF → body; then a bounded send loop handling partial writes and EAGAIN. Does NOT clear @p res — the caller
 * owns the `db_app_response_clear()` call.
 *
 * @param[in,out] cli Client whose buffer and fd are used.
 * @param[in]     res Response to serialize.
 *
 * @return STATUS_SUCCESS when every byte reached the kernel;
 *         STATUS_FAILURE otherwise (connection must be dropped).
 */
int response_writer_send(client_t* cli, const DB_app_response_t* res);

/**
 * @brief Send a minimal static error response (adapter/serializer failures).
 *
 * @param[in,out] cli    Client to answer.
 * @param[in]     status HTTP status (only 500 and 404 carry bodies here).
 *
 * @return STATUS_SUCCESS / STATUS_FAILURE as `response_writer_send()`.
 */
int response_writer_error(client_t* cli, uint16_t status);

#endif /* SERVER_WORKER_CLIENT_RESPONSE_WRITER_H */
