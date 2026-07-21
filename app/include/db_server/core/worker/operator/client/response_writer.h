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
 * @brief Serialize @p res into the client buffer and send it — completely if the socket accepts it
 *        immediately, or PARKED on EPOLLOUT if it doesn't (§9.2 end state).
 *
 * Steps 6-8 of the §9.2 sequence: status line → Content-Type / Content-Length / Connection (from the client's parsed policy) → every extra
 * header on its own line → CRLF → body; then an attempted send. Does NOT clear @p res — the caller owns the `db_app_response_clear()` call
 * (safe regardless of whether the send finished: the body has already been copied into @c cli->buf by the time this returns).
 *
 * When the client's fd is reactor-managed (@c cli->ctx.owner is a live @c operator_t*, i.e. this call came from an operator thread) and the
 * kernel can't accept the whole response right now, this parks: the unsent tail stays in @c cli->buf, @c cli->draining is set, the fd is
 * re-armed for EPOLLOUT via the SAME reactor, and this returns STATUS_SUCCESS — the caller must NOT touch @p cli's connection-policy
 * decision (close vs. keep-alive) itself while `cli->draining` is set; `response_writer_resume()` makes that call once the drain actually
 * finishes. When the fd is NOT reactor-managed (the upload-worker pool, which drives its own blocking `poll()` loop — parking has nothing
 * to park against there), this falls back to the original bounded `poll(POLLOUT)` wait exactly as before.
 *
 * @param[in,out] cli Client whose buffer and fd are used.
 * @param[in]     res Response to serialize.
 *
 * @return STATUS_SUCCESS when every byte reached the kernel OR the send is now parked and draining;
 *         STATUS_FAILURE on a real error (connection must be dropped).
 */
int response_writer_send(client_t* cli, const DB_app_response_t* res);

/**
 * @brief Resume a response send parked by `response_writer_send()`, called when the reactor reports
 *        this client's fd EPOLLOUT-ready.
 *
 * Continues writing @c cli->buf[cli->send_off, cli->send_len) from where it left off. On EAGAIN again,
 * stays parked (returns STATUS_SUCCESS; the reactor will fire again). On completion, clears the drain
 * state, re-arms EPOLLIN for the next request, and — only now — makes the close-vs-keep-alive call this
 * exchange deferred: STATUS_FAILURE means the caller must remove the client (either a real write error,
 * or the client's own `Connection: close`); STATUS_SUCCESS means keep it (EPOLLIN is already re-armed).
 *
 * @param[in,out] cli Client currently draining (`cli->draining` must be 1).
 *
 * @retval STATUS_SUCCESS  still parked, or finished and the connection stays open.
 * @retval STATUS_FAILURE  finished-and-close, or a real send error — caller must remove the client.
 */
int response_writer_resume(client_t* cli);

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
