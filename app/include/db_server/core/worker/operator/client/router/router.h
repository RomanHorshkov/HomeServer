/**
 * @file router.h
 *
 * @brief DB_server prefix router: byte transport → backend handler (§9.1).
 *
 * DB_server routes by prefix only; method filtering and everything semantic happens inside DB_app. The table is a small static const array
 * — no registrar, no constructors. Everything unmatched is an immediate 404 (nginx serves static content; DB_server serves API only).
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    jul 2026
 * (c) 2026
 */
#ifndef SERVER_WORKER_CLIENT_ROUTER_ROUTER_H
#define SERVER_WORKER_CLIENT_ROUTER_ROUTER_H

/*****************************************************************************************************************************************
 * INCLUDES
 *****************************************************************************************************************************************
 */
#include <stddef.h>
#include <stdint.h>

#include <db_server/core/worker/operator/client/client.h>

/*****************************************************************************************************************************************
 * DEFINES
 *****************************************************************************************************************************************
 */

/** @brief Route flag: body is streamed via spool ticket (§9.4). Not used in S2. */
#define SRV_ROUTE_FLAG_STREAM_BODY (1u << 0)

/*****************************************************************************************************************************************
 * ENUMERATED TYPEDEFS
 *****************************************************************************************************************************************
 */

/**
 * @brief Path match strategy for one route entry.
 */
typedef enum
{
    SRV_ROUTE_MATCH_EXACT = 0, /**< path equals route path                    */
    SRV_ROUTE_MATCH_PREFIX     /**< path starts with route path               */
} srv_route_match_t;

/*****************************************************************************************************************************************
 * ENUMERATED VARIABLES
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * STRUCTURED TYPEDEFS
 *****************************************************************************************************************************************
 */

/**
 * @brief One transport-level route entry.
 */
typedef struct
{
    const char*       path;        /**< e.g. "/api/app"                          */
    size_t            path_len;    /**< strlen(path), precomputed                */
    srv_route_match_t match;       /**< exact or prefix                          */
    uint8_t           flags;       /**< SRV_ROUTE_FLAG_* OR-set                  */
    int (*handler)(client_t* cli); /**< returns STATUS_SUCCESS to keep alive  */
} srv_route_t;

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
 * @brief Route one complete parsed request to its transport handler.
 *
 * On a table miss, answers 404 itself and reports failure (close).
 *
 * @param[in,out] cli Client with a complete `http_request` snapshot.
 *
 * @return STATUS_SUCCESS when the response was sent and the connection may
 *         continue per its policy; STATUS_FAILURE when it must drop.
 */
int srv_router_dispatch(client_t* cli);

#endif /* SERVER_WORKER_CLIENT_ROUTER_ROUTER_H */
