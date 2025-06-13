#define _GNU_SOURCE

#include "core.h"

#include <errno.h>       // errno, EADDRINUSE, etc.
#include <netdb.h>       // socklen_t
#include <stdlib.h>      // malloc(), calloc(), NULL etc
#include <string.h>      // memset(), strcpy(), strlen(), etc.
#include <sys/socket.h>  // socklen_t, socket(), bind(), setsockopt(), etc.
// #include <sys/wait.h>    // wait, who hang, pid_t
#include <time.h>        // nanosleep()
#include <unistd.h>      // fork(), close(), pipe(), read(), write(), etc.
#include <pthread.h>     // pthread_create(), pthread_join()

// #include "client_manager.h" ////////////////////// ATTENZIONE QUI!!!!!!!!!!!!!!
// #include "client.h" ////////////////////// ATTENZIONE QUI!!!!!!!!!!!!!!
#include "listener.h"
#include "worker.h"
#include "logger.h"
#include "server_input.h"
#include "server_settings.h"

/****************************************************************************
 * PRIVATE STRUCTURED TYPES
 ****************************************************************************
 */

typedef struct
{
    int pipe_fds[2];                   // pipe for stdin input
    listener_t *listener;
    worker_t *worker;
    pthread_t listener_thread;
    pthread_t worker_thread;
    // future: config_t *config, tls_t *tls, etc.
} server_t;


/****************************************************************************
 * PRIVATE VARIABLES DEFINITIONS
 ****************************************************************************
 */

/* Server's instantiation */
static server_t srv;


/****************************************************************************
 * PRIVATE FUNCTIONS DECLARATIONS
 ****************************************************************************
 */



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
    /* main server run loop */

    /* run the threads */
    pthread_create(&srv.listener_thread, NULL, listener_run, srv.listener);
    pthread_create(&srv.worker_thread, NULL, worker_run, srv.worker);

    /* Wait for threads to finish */
    pthread_join(srv.listener_thread, NULL);
    pthread_join(srv.worker_thread, NULL);



    /* server'ss loop, seeking for q for shutdown */
    // while(check_stdin_for_exit() != 0)
    // {
    //     struct timespec ts;
    //     ts.tv_sec = SERVER_SLEEP_TIME_S;
    //     ts.tv_nsec = SERVER_SLEEP_TIME_NS;
    //     /* usleep and keep checking */
    //     nanosleep(&ts, NULL);

    // }

}

// void server_run(void)
// {
//     /* process id for fork() */
//     pid_t pid;

//     while(check_stdin_for_exit() != 0)
//     {
//         int new_client_socket = accept_client(/* an all listeners */);

//         /* When a new client is incoming manage it */
//         if(new_client_socket != -1)
//         {
//             /* Fork for a new client */
//             pid = fork();

//             /* Child process: */
//             if(pid == 0)
//             {
//                 /* Close all listeners, no need for them in this process */
//                 listener_close(&srv.listener);


//                 /* Exit */
//                 _exit(0);
//             }

//             /* Parent process: */
//             else if(pid > 0)
//             {
//                 /* Set client's process id */
//                 client_manager_set_pid(srv.client_mng, pid, new_client_socket);
//             }
//             else
//             {
//                 log_error("CORE: fork failure: %s", strerror(errno));
//                 /* maybe should close the last client */
//             }
//         }
//         else
//         {
//             struct timespec ts;
//             ts.tv_sec = SERVER_SLEEP_TIME_S;
//             ts.tv_nsec = SERVER_SLEEP_TIME_NS;
//             /* No new client incoming, usleep and keep checking */
//             nanosleep(&ts, NULL);
//         }

//         /* Reap zombie processes */
//         reap_zombies();
//     }
// }

void server_shutdown(void)
{
    listener_shutdown(&srv.listener);
    // worker_shutdown(&srv.worker);
    logger_close();
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
