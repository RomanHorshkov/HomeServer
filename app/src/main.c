/**
 * @file main.c
 * @brief The containerized micro-HTTP server entry point.
 *
 * This function initializes the server and processes incoming requests until shutdown.
 *
 * Usage:
 *   ./server <port>
 *
 * Exit Codes:
 *   0  Clean shutdown
 *   1  Initialization failed
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑05‑11
 * (c) 2025
 */

#ifdef DEBUG
#    include <stdio.h> /* printf / fprintf / perror */
#endif                 /* DEBUG */

#include <stdlib.h> /* getenv */

#include <db_server/core/core.h>      /* server_init / run / shutdown */

/*****************************************************************************************************************************************
 * PRIVATE DEFINES
 *****************************************************************************************************************************************
 */

/*****************************************************************************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

int main(int argc, char* argv[])
{
    /* Usage: ./server <api_spec> [upload_spec]
     *   <api_spec>     TCP port ("3490") or unix path ("/run/home_server/api.sock") for API traffic.
     *   [upload_spec]  optional TCP port ("3492") or unix path for the isolated upload pool; also read from
     *                  DB_SERVER_UPLOAD when omitted. Absent → uploads run in the operators (legacy path). */
    if(argc < 2 || argc > 3)
    {
#ifdef DEBUG
        printf("Usage: %s <api_spec> [upload_spec]\n", argv[0]);
#endif /* DEBUG */
        return 1;
    }

    const char* upload_spec = (argc == 3) ? argv[2] : getenv("DB_SERVER_UPLOAD");

    /* Initialize the server */
    if(server_init(argv[1], upload_spec) != 0) return 1;

    /* Run the server */
    server_run();

    return 0;
}
