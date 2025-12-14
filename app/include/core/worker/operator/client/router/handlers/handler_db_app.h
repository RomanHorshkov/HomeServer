/**
 * @file    handler_db_app.h
 * 
 * @brief   Database application HTTP handler interface.
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025-05-11
 */
#ifndef SERVER_ROUTER_HANDLER_DB_APP_H
#define SERVER_ROUTER_HANDLER_DB_APP_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */

#include "http_common.h"   /* http_request_t, http_response_t, method, ... */

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

int handler_database(const http_request_t* req, http_response_t* res);

#endif /* SERVER_ROUTER_HANDLER_DB_APP_H */
