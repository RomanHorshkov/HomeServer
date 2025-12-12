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
#include <limits.h>
#include <netdb.h>      /* socklen_t */
#include <pthread.h>    /* pthread_create(), pthread_join() */
#include <pwd.h>        /* pwd */
#include <stdbool.h>
#include <stdio.h>      /* printf(), fprintf(), etc. */
#include <stdlib.h>     /* malloc(), calloc(), NULL etc */
#include <sys/socket.h> /* socklen_t, socket(), bind(), setsockopt(), etc. */
#include <sys/types.h>
#include <unistd.h> /* fork(), close(), pipe(), read(), write(), getlogin(), getcwd(), system() etc. */

#include "emlog.h"
#include "listener.h"
#include "worker.h"
#include "config_core.h"
#include "socket_helper.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "srv_core"

/****************************************************************************
 * PRIVATE STRUCTURED TYPES
 ****************************************************************************
 */

 /**
  * 
  * idea: core possesses listener, worker, and pipeline.
  * then, listener and worker gets pointer to pipeline from core during init.
  * core also manages the threads for listener, worker.
  */
typedef struct
{
    /* listener thread */
    pthread_t listener_thread;

    /* Detected CPU count for sizing worker/operator threads */
    uint8_t cpu_count;

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

static void _core_logger_bootstrap(void);

static uint8_t _core_detect_cpu_count(void);

#ifdef MODE_DEBUG
static void _p_dbg_info_init(void);
#endif

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int server_init(const char *port)
{
#ifdef MODE_DEBUG
    _p_dbg_info_init();
#endif /* MODE_DEBUG */
    
    _core_logger_bootstrap();

    /* Detect available CPUs and keep the value for thread sizing */
    server.cpu_count = _core_detect_cpu_count();

    /* Initialize the listener */
    if(listener_init(port/*, server.pipeline*/) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "listener failed to init.");
        goto fail;
    }

    /* Initialize the worker */
    if(worker_init(server.cpu_count) != 0)
    {
        EML_PERR(LOG_TAG, "worker failed to init.");
        goto fail;
    }

    /* Successful initialization */
    EML_INFO(LOG_TAG, "🚀 C Server running on http://localhost:%s", port);
    return STATUS_SUCCESS;
fail:
    return STATUS_FAILURE;
}

void server_run(void)
{
    /* run listener thread */
    pthread_create(&server.listener_thread, NULL, listener_run, NULL);

    /* run operators threads */
    worker_run();

    /* wait listener thread */
    pthread_join(server.listener_thread, NULL);

    worker_destroy();
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static void _core_logger_bootstrap(void)
{
    static bool initialized = false;
    if(initialized) return;

#ifdef MODE_DEBUG
    emlog_init(EML_LEVEL_DBG, true);
    emlog_set_writev_flush(true);
    EML_INFO(LOG_TAG, "Debug logger active; stdout/stderr sink");
#else
    /* Production will go to journald */
    emlog_init(EML_LEVEL_INFO, false);
    emlog_set_writev_flush(false);
    EML_INFO(LOG_TAG, "Production logger active; journald sink");
#endif

    initialized = true;
}

static uint8_t _core_detect_cpu_count(void)
{
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);

    EML_INFO(LOG_TAG, "Detected %ld CPU%s available",
             cpus, (cpus == 1) ? "" : "s");
    if(cpus < 1)
    {
        EML_PERR(LOG_TAG, "sysconf(_SC_NPROCESSORS_ONLN) failed, defaulting to 1 CPU");
        return 1;
    }
    if (cpus > 255)
    {
        EML_WARN(LOG_TAG, "Detected CPU count %ld exceeds max supported 255, capping to 255", cpus);
        return 255;
    }    

    return (uint8_t)cpus;
}

#ifdef MODE_DEBUG
static void _p_dbg_info_init(void)
{
    
    // all this is a partial representation of the server's core folder and
    // what is visible in it, from which user it is running, and the current working directory.
    /* Print current user */
    uid_t uid = geteuid();
    const struct passwd *pw = getpwuid(uid);
    if(pw)
    {
        printf(LOG_TAG "running as user: %s\n", pw->pw_name);
    }
    else
    {
        printf(LOG_TAG "running as user: UNKNOWN (uid=%d)\n", (int)uid);
    }

    /* Print current working directory */
    char cwd[PATH_MAX];
    if(getcwd(cwd, sizeof(cwd)) != NULL)
        printf(LOG_TAG "cwd: %s\n", cwd);

    /* List directory contents */
    printf(LOG_TAG "ls -la:\n");
    system("ls -la");
}
#endif
