#define _GNU_SOURCE

#include "core.h"

#include <errno.h>          /* errno, EADDRINUSE, stdout, stdin. */
#include <netdb.h>          /* socklen_t */
#include <stdio.h>          /* printf(), fprintf(), etc. */
#include <stdlib.h>         /* malloc(), calloc(), NULL etc */
#include <string.h>         /* memset(), strcpy(), strlen(), etc. */
#include <sys/socket.h>     /* socklen_t, socket(), bind(), setsockopt(), etc. */
#include <unistd.h>         /* fork(), close(), pipe(), read(), write(), etc. */
#include <pthread.h>        /* pthread_create(), pthread_join() */


#include "listener.h"
#include "worker.h"
#include "logger.h"
#include "server_settings.h"

/****************************************************************************
 * PRIVATE STRUCTURED TYPES
 ****************************************************************************
 */

typedef struct
{
    /* pipe for communication between listener and worker */
    int pipe_fds[2];

    /* listener instance */
    listener_t *listener;

    /* worker instance */
    worker_t *worker;

    /* listener thread */
    pthread_t listener_thread;

    /* worker thread */
    pthread_t worker_thread;

    /* control thread */
    pthread_t control_thread;

    // future: config_t *config, tls_t *tls, etc.
} server_t;


/****************************************************************************
 * PRIVATE VARIABLES DEFINITIONS
 ****************************************************************************
 */

/**
 * @brief Singleton instance of the main server structure.
 *
 * Holds all core components (listener, worker, control threads, and pipe)
 * for the lifetime of the server process. This instance is private to the
 * core module and should not be accessed directly from outside.
 */
static server_t srv;


/****************************************************************************
 * PRIVATE FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief Control thread entry point: handles interactive server menu and shutdown.
 *
 * Presents a simple menu on the controlling terminal, allowing the operator
 * to inspect server state (listeners, clients, etc.) and to initiate a
 * graceful shutdown by typing 'q' + ENTER. This function is intended to be
 * run in a dedicated thread and blocks until shutdown is requested.
 *
 * @param arg  Unused (may be NULL).
 * @return     NULL on exit.
 */
void *control_run(void *arg);


/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int server_init(const char *port)
{
    /* return value */
    int ret = STATUS_FAILURE;

    /* Initialize the pipe between listener and worker */
    if (pipe(srv.pipe_fds) == -1)
    {
        log_error("CORE: pipe failed to create: %s", strerror(errno));
    }
    /* Set the pipe file descriptors to non-blocking */
    else if (set_socket_non_blocking(&srv.pipe_fds[0]) != STATUS_SUCCESS)
    {
        log_error("CORE: set_socket_non_blocking failed for pipe 0.");
    }
    else if(set_socket_non_blocking(&srv.pipe_fds[1]) != STATUS_SUCCESS)
    {
        log_error("CORE: set_socket_non_blocking failed for pipe 1.");
    }
    else
    {
        /* Initialize the logger */
        logger_init("server.log");

        /* Initialize the listener */
        if(listener_init(&srv.listener, port, &srv.pipe_fds[1]) != STATUS_SUCCESS)
        {
            log_error("CORE: listener failed to init.", strerror(errno));
        }

        /* Initialize the worker */
        if(worker_init(&srv.worker, &srv.pipe_fds[0]) != STATUS_SUCCESS)
        {
            log_error("CORE: worker failed to init.", strerror(errno));
        }

        /* Successful initialization */
        else
        {
            log_info("🚀 C Server running on http://localhost:%s\n", port);
            ret = STATUS_SUCCESS;
        }
    }

    return ret;
}

void server_run(void)
{
    /* run threads */
    pthread_create(&srv.listener_thread, NULL, listener_run, srv.listener);
    pthread_create(&srv.worker_thread, NULL, worker_run, srv.worker);
    pthread_create(&srv.control_thread, NULL, control_run, NULL);

    /* wait threads */
    pthread_join(srv.listener_thread, NULL);
    pthread_join(srv.worker_thread, NULL);
    pthread_join(srv.control_thread, NULL);
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

void *control_run(void *arg)
{
    (void)arg; // Unused parameter

    /* input from console */
    char input[16];
    while (1)
    {
        printf("\n--- Server Menu ---\n");
        printf("1. Listeners\n");
        printf("2. Clients\n");
        printf("q. Shutdown\n");
        printf("Select: ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) continue;

        if (input[0] == '1')
        {
            printf("Show listeners info here...\n");
        }
        else if (input[0] == '2')
        {
            printf("Show clients info here...\n");
        }
        else if (input[0] == 'q')
        {
            printf("Shutting down server...\n");
            /* Set listener and worker status to shutdown */
            worker_set_status(srv.worker, SERVER_STATUS_SHUTDOWN);
            listener_set_status(srv.listener, SERVER_STATUS_SHUTDOWN);
            break;
        }
        else
        {
            printf("Unknown option.\n");
        }
    }
    return NULL;
}
