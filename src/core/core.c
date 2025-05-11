#define _GNU_SOURCE

#include "core.h"

#include <errno.h>       // errno, EADDRINUSE, etc.
#include <netdb.h>       // socklen_t
#include <stdlib.h>      // malloc(), calloc(), NULL etc
#include <string.h>      // memset(), strcpy(), strlen(), etc.
#include <sys/socket.h>  // socklen_t, socket(), bind(), setsockopt(), etc.
#include <sys/time.h>    // struct timeval
#include <sys/wait.h>    // wait, who hang, pid_t
#include <time.h>        // nanosleep()
#include <unistd.h>      // fork(), close()

#include "client_manager.h"
#include "listener.h"
#include "logger.h"
#include "server_input.h"
#include "server_settings.h"

/****************************************************************************
 * PRIVATE STRUCTURED TYPES
 ****************************************************************************
 */

typedef struct server
{
    Listener_t *listener;
    client_manager_t *client_mng;
    /* future: config_t *, tls_ctx_t *, … */
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

static int accept_client(void);

static void reap_zombies(void);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int server_init(const char *port)
{
    /* return value */
    int ret = -1;

    /* Start the logger */
    logger_init("server.log");

    /* Initialize the listener */
    if(listener_init(&srv.listener, port) != -1)
    {
        /* Initialize the clients manager */
        srv.client_mng = client_manager_new((size_t)MAX_CLIENTS);

        /* check if initialization succeded */
        if(srv.client_mng != NULL)
        {
            /* Successful initialization */
            log_info("🚀 C Server running on http://localhost:%s\n", port);
            ret = 0;
        }
        else
        {
            log_error("CORE: clients manager failed to init.", strerror(errno));
        }
    }
    else
    {
        log_error("CORE: listener failed to init.", strerror(errno));
    }

    return ret;
}

void server_run(void)
{
    /* process id for fork() */
    pid_t pid;

    while(check_stdin_for_exit() != 0)
    {
        int new_socket_fd = accept_client(/* an all listeners */);

        if(new_socket_fd != -1)
        {
            /* Fork for a new client */
            pid = fork();

            /* Child process: */
            if(pid == 0)
            {
                /* Close all listeners, no need for them in this process */
                listener_close(&srv.listener);

                /* Handle client: blocking function */
                client_manager_handle_socket(new_socket_fd);

                /* Exit */
                _exit(0);
            }

            /* Parent process: */
            else if(pid > 0)
            {
                /* Set client's process id */
                client_manager_set_pid(srv.client_mng, pid, new_socket_fd);
            }
            else
            {
                log_error("CORE: fork failure: %s", strerror(errno));
                /* maybe should close the last client */
            }
        }
        else
        {
            struct timespec ts;
            ts.tv_sec = SERVER_SLEEP_TIME_S;
            ts.tv_nsec = SERVER_SLEEP_TIME_NS;
            /* No new client incoming, usleep and keep checking */
            nanosleep(&ts, NULL);
        }

        /* Reap zombie processes */
        reap_zombies();
    }
}

void server_shutdown(void)
{
    listener_shutdown(&srv.listener);
    client_manager_free(srv.client_mng);
    logger_close();
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int accept_client(void)
{
    /* return value */
    int client_fd = -1;

    /* client socket storage structure */
    struct sockaddr_storage client_addr;

    /* client socket storage structure length */
    socklen_t client_addr_len = sizeof(client_addr);

    /* Pass the variables by reference because the client_add_len for example, written by accept()
    in the listener, is setting the length of the client address, which is important later
    for managing the client itself. */
    if(listener_check_incoming_clients(&srv.listener, &client_addr, &client_addr_len, &client_fd) ==
       -1)
    {
        // do nothing, no incoming sockets connection requests
    }

    /* save and manage new socket */
    else if(client_manager_add_socket(srv.client_mng, &client_addr, client_addr_len, client_fd) ==
            -1)
    {
        // do nothing
    }

    return client_fd;
}

static void reap_zombies(void)
{
    pid_t reaped_pid;

    /* -1 to check any child,
    WHOHANG doesn't block if no child dead,
    returns:
    child's PID if any dies, 0 if none, -1 if error */
    while((reaped_pid = waitpid(-1, NULL, WNOHANG)) > 0)
    {
        client_manager_reap(srv.client_mng, reaped_pid);
    }
}
