/**
 * @file handler_db_app.h
 *
 * @brief Transport handler bridging one client request into DB_app (§9.3).
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    jul 2026
 * (c) 2026
 */
#ifndef SERVER_WORKER_CLIENT_ROUTER_HANDLERS_HANDLER_DB_APP_H
#define SERVER_WORKER_CLIENT_ROUTER_HANDLERS_HANDLER_DB_APP_H

/****************************************************************************
 * INCLUDES
 ****************************************************************************
 */
#include "../../client.h"

/****************************************************************************
 * DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * ENUMERATED TYPEDEFS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * ENUMERATED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * STRUCTURED TYPEDEFS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * STRUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief Adapt the parsed request, run DB_app, serialize and send the answer.
 *
 * The §9.3 sequence: `db_app_request_from_db_http()` →
 * `db_app_response_init()` → `db_app_run()` → `response_writer_send()` →
 * `db_app_response_clear()` (always).
 *
 * @param[in,out] cli Client with a complete `http_request` snapshot.
 *
 * @return STATUS_SUCCESS when the response was fully sent;
 *         STATUS_FAILURE when the connection must drop.
 */
int handler_db_app(client_t* cli);

#endif /* SERVER_WORKER_CLIENT_ROUTER_HANDLERS_HANDLER_DB_APP_H */
