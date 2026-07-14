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
 * @author  Roman Horshkov <124358264+RomanHorshkov@users.noreply.github.com>
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
    /* Two ways to get listening sockets (DB_server/README.md):
     *
     *   PRODUCTION — systemd socket activation. The service is started by api.socket/upload.socket, which
     *   pass the listening fds via LISTEN_FDS. NO argv is needed (and none is given); server_init detects
     *   the activation environment and adopts the fds.
     *
     *   DEV / direct — ./server <api_spec> [upload_spec], where a spec is a TCP port ("3491") or a unix
     *   path. upload_spec also comes from DB_SERVER_UPLOAD when omitted. Absent upload spec → uploads run
     *   in the operators (legacy path).
     *
     * argv is optional: when socket-activated we accept argc==1. server_init errors if it is neither
     * activated nor given a spec. */
    if(argc > 3)
    {
#ifdef DEBUG
        printf("Usage: %s [<api_spec> [upload_spec]]   (no args when socket-activated)\n", argv[0]);
#endif /* DEBUG */
        return 1;
    }

    const char* api_spec    = (argc >= 2) ? argv[1] : NULL;
    const char* upload_spec = (argc == 3) ? argv[2] : getenv("DB_SERVER_UPLOAD");

    /* Initialize the server */
    if(server_init(api_spec, upload_spec) != 0) return 1;

    /* Run the server */
    server_run();

    return 0;
}
