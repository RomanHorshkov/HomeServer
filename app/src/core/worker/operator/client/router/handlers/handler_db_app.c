/**
 * @file handler_db_app.c
 *
 * @brief The DB_app bridge: adapt → run → serialize → clear (§9.3).
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    jul 2026
 * (c) 2026
 */

/****************************************************************************
 * PRIVATE INCLUDES
 ****************************************************************************
 */
#include <emlog.h>

#include <db_app.h>

#include "handler_db_app.h"

#include "../../response_writer.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
#define LOG_TAG "srv_handler_db_app"

/****************************************************************************
 * PRIVATE ENUMERATED TYPEDEFS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STRUCTURED TYPEDEFS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int handler_db_app(client_t* cli)
{
    if(!cli)
    {
        EML_ERROR(LOG_TAG, "NULL client");
        return STATUS_FAILURE;
    }

    DB_app_request_t req;
    if(db_app_request_from_db_http(&cli->http_request, &req) != 0)
    {
        EML_ERROR(LOG_TAG, "fd %d: request adapt failed", cli->ctx.fd);
        return response_writer_error(cli, 500u);
    }

    DB_app_response_t res;
    db_app_response_init(&res);
    (void)db_app_run(&req, &res); /* res always carries the outcome */

    int rc = response_writer_send(cli, &res);
    db_app_response_clear(&res);
    return rc;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
/* None */
