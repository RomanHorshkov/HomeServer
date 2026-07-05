/**
 * @file router.c
 *
 * @brief Static prefix route table and transport dispatch (§9.1).
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    jul 2026
 * (c) 2026
 */

/*****************************************************************************************************************************************
 * PRIVATE INCLUDES
 *****************************************************************************************************************************************
 */
#include <string.h>

#include <emlog.h>

#include <db_server/core/worker/operator/client/router/handlers/handler_db_app.h>
#include <db_server/core/worker/operator/client/router/router.h>

#include <db_server/core/worker/operator/client/response_writer.h>

/*****************************************************************************************************************************************
 * PRIVATE DEFINES
 *****************************************************************************************************************************************
 */
#define LOG_TAG "srv_router"

/*****************************************************************************************************************************************
 * PRIVATE ENUMERATED TYPEDEFS
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * PRIVATE STRUCTURED TYPEDEFS
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * PRIVATE VARIABLES
 *****************************************************************************************************************************************
 */

/**
 * @brief The transport route table. `/api/app` prefix → DB_app; that's all.
 *
 * `/api/app` PREFIX also matches "/api/app" exactly and "/api/app/..." — DB_app's own router rejects foreign lookalikes ("/api/appx") after
 * normalization.
 */
static const srv_route_t g_routes[] = {
    {.path = "/api/app", .path_len = 8u, .match = SRV_ROUTE_MATCH_PREFIX, .flags = 0u, .handler = handler_db_app},
};

#define SRV_ROUTES_COUNT (sizeof(g_routes) / sizeof(g_routes[0]))

/*****************************************************************************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 *****************************************************************************************************************************************
 */

static int _route_matches(const srv_route_t* route, const char* path, size_t path_len);

/*****************************************************************************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

int srv_router_dispatch(client_t* cli)
{
    if(!cli || !cli->http_request.message_complete)
    {
        EML_ERROR(LOG_TAG, "dispatch: no complete request");
        return STATUS_FAILURE;
    }

    const char*  path     = cli->http_request.path.p;
    const size_t path_len = cli->http_request.path.n;
    if(!path || path_len == 0u)
    {
        EML_ERROR(LOG_TAG, "fd %d: empty path", cli->ctx.fd);
        return response_writer_error(cli, 404u);
    }

    for(size_t i = 0u; i < SRV_ROUTES_COUNT; i++)
    {
        if(_route_matches(&g_routes[i], path, path_len))
        {
            return g_routes[i].handler(cli);
        }
    }

    EML_ERROR(LOG_TAG, "fd %d: no route for '%.*s'", cli->ctx.fd, (int)path_len, path);
    return response_writer_error(cli, 404u);
}

/*****************************************************************************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

/**
 * @brief Exact or prefix match; prefix requires end-of-path, '/' or '?' after it.
 */
static int _route_matches(const srv_route_t* route, const char* path, size_t path_len)
{
    if(path_len < route->path_len || memcmp(path, route->path, route->path_len) != 0)
    {
        return 0;
    }
    if(route->match == SRV_ROUTE_MATCH_EXACT)
    {
        return path_len == route->path_len;
    }
    return path_len == route->path_len || path[route->path_len] == '/' || path[route->path_len] == '?';
}
