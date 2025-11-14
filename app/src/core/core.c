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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE */

#include <errno.h>
#include <netdb.h>      /* socklen_t */
#include <pthread.h>    /* pthread_create(), pthread_join() */
#include <pwd.h>        /* pwd */
#include <stdbool.h>
#include <stdio.h>      /* printf(), fprintf(), etc. */
#include <stdlib.h>     /* malloc(), calloc(), NULL etc */
#include <sys/socket.h> /* socklen_t, socket(), bind(), setsockopt(), etc. */
#include <sys/types.h>
#include <unistd.h> /* fork(), close(), pipe(), read(), write(), getlogin(), getcwd(), system() etc. */

#include <emlog.h>
#include "db_app/db_app.h"
#include "listener.h"
#include "pipeline.h"
#include "router.h"
#include "server_settings.h"
#include "socket_helper.h"
#include "worker.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "core"

/****************************************************************************
 * PRIVATE STRUCTURED TYPES
 ****************************************************************************
 */

typedef struct
{
    /* listener instance */
    listener_t *listener;

    /* worker instance */
    worker_t *worker;

    /* collaboration structure */
    pipeline_t *pipeline;

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
static server_t server;

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

#ifdef DEBUG_MODE
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
#endif /* DEBUG_MODE */

static void core_logger_bootstrap(void);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int server_init(const char *port)
{
    /* return value */
    int ret = STATUS_FAILURE;

#ifdef DEBUG_MODE
    // all this is a partial representation of the server's core folder and
    // what is visible in it, from which user it is running, and the current working directory.
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

    core_logger_bootstrap();

    /* Initialize the external database application */
    if(db_app_init() != 0)
    {
        EML_ERROR(LOG_TAG, "db_app_init failed");
        return -1;
    }

    /* Initialize the pipeline between listener and worker */
    if(pipeline_init(&server.pipeline) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "W <-> L pipeline communication_init failed.");
    }

    /* Initialize the listener with port, pipe read end, wakeup_fd, and ring */
    else if(listener_init(&server.listener, port, server.pipeline) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "listener failed to init.");
    }

    /* Initialize the worker */
    else if(worker_init(&server.worker, server.pipeline) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "worker failed to init.");
    }

    /* Successful initialization */
    else
    {
        EML_INFO(LOG_TAG, "🚀 C Server running on http://localhost:%s", port);
        ret = STATUS_SUCCESS;
    }

    return ret;
}

void server_run(void)
{
    /* run threads */
    pthread_create(&server.listener_thread, NULL, listener_run, server.listener);
    pthread_create(&server.worker_thread, NULL, worker_run, server.worker);
#ifdef DEBUG_MODE
    pthread_create(&server.control_thread, NULL, control_run, NULL);
#endif /* DEBUG_MODE */

    /* wait threads */
    pthread_join(server.listener_thread, NULL);
    pthread_join(server.worker_thread, NULL);
#ifdef DEBUG_MODE
    pthread_join(server.control_thread, NULL);
#endif /* DEBUG_MODE */

    pipeline_destroy(&server.pipeline);
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
            /* Set listener and worker status to shutdown */
            worker_set_status(server.worker, WORKER_STATUS_SHUTDOWN);
            listener_set_status(server.listener, LISTENER_STATUS_SHUTDOWN);
            EML_INFO(LOG_TAG, "Control thread requested shutdown via console menu");
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

static void core_logger_bootstrap(void)
{
    static bool initialized = false;
    if(initialized) return;

#ifdef DEBUG_MODE
    emlog_init(EML_LEVEL_DBG, true);
    emlog_set_writev_flush(true);
    EML_INFO(LOG_TAG, "Debug logger active; stdout/stderr sink");
#else
    emlog_init(EML_LEVEL_INFO, true);
    emlog_set_writev_flush(true);
    EML_INFO(LOG_TAG, "Production logger active; stdout/stderr sink");
#endif

    initialized = true;
}
