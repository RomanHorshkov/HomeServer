/**
 * @file main.c
 * @brief The containerized micro-HTTP server entry point.
 *
 * This function initializes the server and processes incoming requests
 * until shutdown.
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

#ifdef MODE_DEBUG
#    include <stdio.h> /* printf / fprintf / perror */
#endif                 /* MODE_DEBUG */

#include "core.h" /* server_init / run / shutdown */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int main(int argc, char *argv[])
{
    /* Check input */
    if(argc != 2)
    {
#ifdef MODE_DEBUG
        printf("Usage: %s <port>\n", argv[0]);
#endif /* MODE_DEBUG */
        return -1;
    }

    /* Initialize the server */
    else if(server_init(argv[1]) != 0) return -1;

    /* Run the server */
    else
    {
        server_run();
    }

    return 0;
}
