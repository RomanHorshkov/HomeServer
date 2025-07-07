/**
 * @file main.c
 * @brief The containerized micro-HTTP server entry point.
 *
 * This function sets the working directory, initializes the server,
 * and processes incoming requests until shutdown.
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

#include <stdio.h>  /* printf / fprintf / perror                          */
#include <unistd.h> /* chdir                                              */

#include "core.h" /* server_init / run / shutdown                       */
// #include "logger.h" /* loginfo / log_error / logger_init                  */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
#ifndef FHS_RELEASE
#    define CHDIR_PATH "var/www"  // DO NOT WRITE /var/www as global path
#else
#    define CHDIR_PATH "/srv/HomeServer/www"
#endif /* FHS_RELEASE */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int main(int argc, char *argv[])
{
    /* Return variable */
    int res = -1;

    /* Check input */
    if(argc != 2)
    {
        printf("Usage: %s <port>\n", argv[0]);
    }

    /* Set working directory */
    else if(chdir(CHDIR_PATH) != 0)
    {
        printf("chdir to %s failed", CHDIR_PATH);
    }

    /* Initialize the server */
    else if(server_init(argv[1]) == -1)
    {
        /* do nothing, server not initialized */
    }

    /* Run the server */
    else
    {
        printf("Server started on port %s...", argv[1]);
        server_run();

        /* Set return variable to success */
        res = 0;
    }

    return res;
}
