#define _GNU_SOURCE
/**
 * @file router.c
 * @brief HTTP request router implementation.
 *
 * Dispatches incoming requests to APIs' handlers.
 * Provides unified 404/405 responses when needed.
 *
 * Exit Codes:
 *   STATUS_SUCCESS  (0)
 *   STATUS_FAILURE  (1)
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑05‑11
 * (c) 2025
 */

#include "router.h"

#include <string.h> /* for memcmp */
#include "route_register.h"
#include "emlog.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "srv_router"

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTION PROTOTYPES
 ****************************************************************************
 */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int router_handle_request(const http_request_t *request, http_response_t *response)
{
    /* Check input request validity */
    if(!request || !request->message_complete)
    {
        EML_ERROR(LOG_TAG, "_handle_request: invalid request");
        return STATUS_FAILURE;
    }

    /* Check input response validity */
    if(!response || !response->send_sv.p)
    {
        EML_ERROR(LOG_TAG, "_handle_request: invalid response");
        return STATUS_FAILURE;
    }
    
#ifdef MODE_DEBUG
    EML_DBG(LOG_TAG, "_handle_request: handling request for path %.*s",
            (int)request->path.n,
            request->path.p ? request->path.p : "");
#endif
    
    size_t n_routes = 0;
    const route_t *route = router_get_table(&n_routes);

    if (n_routes != 0 && route != NULL)
    {
        for (size_t i = 0; i < n_routes; ++i)
        {
            if (request->path.n == route[i].path_len &&
                memcmp(request->path.p, route[i].path, route[i].path_len) == 0)
            {
#ifdef MODE_DEBUG
                EML_DBG(LOG_TAG, "_handle_request: found route for path %.*s, executing.",
                        (int)request->path.n,
                        request->path.p ? request->path.p : "");
#endif
                return route[i].handler(request, response);
            }
        }
    }

    return STATUS_FAILURE;   
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
/* None */
