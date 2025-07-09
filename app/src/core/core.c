#define _GNU_SOURCE

#include "core.h"

/**
 * @file core.c
 * @brief Core server logic and concurrency management.
 *
 * This module manages the primary components of the micro-HTTP server,
 * including the listener, worker, and control threads. It provides a
 * unified interface for initialization and runtime operations.
 *
 * Usage:
 *   server_init(port);
 *   server_run();
 *
 * Exit Codes:
 *   STATUS_SUCCESS  (0)
 *   STATUS_FAILURE  (1)
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑05‑11
 * (c) 2025
 */

#include <netdb.h>      /* socklen_t */
#include <pthread.h>    /* pthread_create(), pthread_join() */
#include <pwd.h>        /* pwd */
#include <stdio.h>      /* printf(), fprintf(), etc. */
#include <stdlib.h>     /* malloc(), calloc(), NULL etc */
#include <sys/socket.h> /* socklen_t, socket(), bind(), setsockopt(), etc. */
#include <sys/types.h>
#include <unistd.h> /* fork(), close(), pipe(), read(), write(), getlogin(), getcwd(), system() etc. */

#include "listener.h"
#include "logger.h"
#include "server_settings.h"
#include "socket_helper.h"
#include "worker.h"

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
 * PRIVATE FUNCTIONS PROTOTYPES
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

#ifdef DEBUG_MODE
    /* Print current user */
    uid_t uid = geteuid();
    const struct passwd *pw = getpwuid(uid);
    if(pw)
    {
        printf("[CORE]: running as user: %s\n", pw->pw_name);
    }
    else
    {
        printf("[CORE]: running as user: UNKNOWN (uid=%d)\n", (int)uid);
    }

    /* Print current working directory */
    char cwd[HTTP_MAX_PATH_LEN];
    if(getcwd(cwd, sizeof(cwd)) != NULL) printf("[CORE]: cwd: %s\n", cwd);

    /* List directory contents */
    printf("[CORE]: ls -la:\n");
    system("ls -la");
#endif /* DEBUG_MODE */

    printf("[CORE]: Initializing pipe\n");
    /* Initialize the pipe between listener and worker */
    if(pipe(srv.pipe_fds) == -1)
    {
        log_error("[CORE] pipe failed to create: %s", strerror(errno));
    }

    printf("[CORE]: Setting pipe file descriptor 0 to non-blocking\n");
    /* Set the pipe file descriptors to non-blocking */
    if(pipe_fd_set_non_blocking(&srv.pipe_fds[0]) != STATUS_SUCCESS)
    {
        log_error("[CORE] pipe_fd_set_non_blocking failed for pipe 0.");
    }

    printf("[CORE]: Setting pipe file descriptor 1 to non-blocking\n");
    if(pipe_fd_set_non_blocking(&srv.pipe_fds[1]) != STATUS_SUCCESS)
    {
        log_error("[CORE] pipe_fd_set_non_blocking failed for pipe 1.");
    }

    else
    {
        printf("[CORE]: Setting logger\n");
        /* Initialize the logger */
        logger_init("server.log");

        /* Initialize the listener */
        if(listener_init(&srv.listener, port, &srv.pipe_fds[1]) != STATUS_SUCCESS)
        {
            log_error("[CORE] listener failed to init.", strerror(errno));
        }

        /* Initialize the worker */
        else if(worker_init(&srv.worker, &srv.pipe_fds[0]) != STATUS_SUCCESS)
        {
            log_error("[CORE] worker failed to init.", strerror(errno));
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
#ifdef DEBUG_MODE
    pthread_create(&srv.control_thread, NULL, control_run, NULL);
#endif /* DEBUG_MODE */

    /* wait threads */
    pthread_join(srv.listener_thread, NULL);
    pthread_join(srv.worker_thread, NULL);
#ifdef DEBUG_MODE
    pthread_join(srv.control_thread, NULL);
#endif /* DEBUG_MODE */
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

#ifdef DEBUG_MODE
void *control_run(void *arg)
{
    (void)arg;  // Unused parameter

    /* input from console */
    char input[16];
    while(1)
    {
        printf("\n--- Server Menu ---\n");
        printf("1. Listeners\n");
        printf("2. Clients\n");
        printf("q. Shutdown\n");
        printf("Select: ");
        fflush(stdout);

        if(fgets(input, sizeof(input), stdin) == NULL) continue;

        if(input[0] == '1')
        {
            printf("Show listeners info here...\n");
        }
        else if(input[0] == '2')
        {
            printf("Show clients info here...\n");
        }
        else if(input[0] == 'q')
        {
            printf("Shutting down server...\n");
            /* Set listener and worker status to shutdown */
            worker_set_status(srv.worker, SERVER_STATUS_SHUTDOWN);
            listener_set_status(srv.listener, SERVER_STATUS_SHUTDOWN);
            logger_close();
            break;
        }
        else
        {
            printf("Unknown option.\n");
        }
    }
    return NULL;
}
#endif /* DEBUG_MODE */
