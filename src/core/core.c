#define _POSIX_C_SOURCE 200112L
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

#include "client.h"
#include "listener.h"
#include "logger.h"
#include "server_input.h"
#include "server_settings.h"

/****************************************************************************
 * PRIVATE STRUCTURED TYPES
 ****************************************************************************
 */

// /* Server's clients */
// typedef struct
// {
//     int client_fd;
//     int pid;
//     char host[NI_MAXHOST];
//     char service[NI_MAXSERV];
// }
// Client_t;

/****************************************************************************
 * PRIVATE VARIABLES DEFINITIONS
 ****************************************************************************
 */

/* Server's listener */
static Listener_t *listener = NULL;

/* Server's clients */
static clients_t *clients;

// static Client_t *clients = NULL;
// static int active_clients_no = 0;

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

    /* init the listener */
    if(listener_init(&listener, port) == -1)
    {
        /* TO DO */
    }
    else if(clients_init(&clients) == -1)
    {
        /* TO DO */
    }
    else
    {
        log_info("🚀 C Server running on http://localhost:%s\n", port);
        ret = 0;
    }

    return ret;
}

void server_run(void)
{
    pid_t pid;

    while(check_stdin_for_exit() != 0)
    {
        int client_fd = accept_client(/* an all listeners */);

        if(client_fd != -1)
        {
            /* Fork for a new client */
            pid = fork();

            if(pid == 0)
            {
                /*
                Child process:
                Start child process and handle the client
                */
                /* Close all listeners, no need for them in this process */
                listener_close(&listener);

                /* Handle client:
                blocking function */
                clients_handle_client(&client_fd);

                /* Exit */
                _exit(0); /* child only */
            }
            else if(pid > 0)
            {
                /*
                Parent process:
                */

                /* Set client's process id */
                clients_set_client_pid(&clients, &pid, &client_fd);

                /* close all clients to keep the
                parent clean just with listeners */
                // close(client_fd);
                // clients_close_client(&clients, &client_fd);
            }
            else
            {
                log_error("fork: %s", strerror(errno));
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
    listener_shutdown(&listener);
    clients_shutdown(&clients);
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

    /* helper structures */
    struct sockaddr_storage client_addr;

    if(listener_check_incoming_clients(&listener, &client_addr, &client_fd) == -1)
    {
        // do nothing, no incoming clients
    }

    /* save and manage new client */
    else if(clients_add_new_client(&clients, &client_addr, &client_fd) == -1)
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
        clients_erase_client(&clients, &reaped_pid);
    }
}
