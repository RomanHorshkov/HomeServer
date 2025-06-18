#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include "worker.h" /* worker */

#include <errno.h>     /* errno, EADDRINUSE, etc. */
#include <fcntl.h>     /* fcntl(), F_GETFL, F_SETFL, O_NONBLOCK */
#include <netdb.h>     /* getaddrinfo(), addrinfo, gai_strerror() */
#include <stdatomic.h> /* atomic_int */
#include <stdlib.h>    /* malloc(), calloc() etc */
#include <string.h>    /* memset(), strcpy(), strlen(), etc. */
#include <sys/epoll.h> /* epoll_create1(), epoll_ctl(), epoll_wait(), struct epoll_event */
#include <unistd.h>    /* fork(), close(), read(), write(), etc. */

#include "browser.h"         /* browser_manage_client_req */
#include "logger.h"          /* logger */
#include "server_settings.h" /* settings */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */

struct worker
{
    /* sockets file descriptors */
    int sockets_fds[MAX_CLIENTS];

    /* active listening sockets */
    int active_sockets_no;

    /* pipe read fd */
    int pipe_read_fd;

    /* status variable */
    atomic_int status; /* 0 = inactive, 1 = active */
};

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int worker_init(worker_t **worker_ptr, int *pipe_read_fd)
{
    log_info("Starting Worker...");

    /* return value */
    int res = STATUS_FAILURE;

    /* Allocate memory for the worker */
    *worker_ptr = (worker_t *)calloc(1, sizeof(worker_t));

    /* Check input */
    if(pipe_read_fd == NULL)
    {
        log_error("worker_init: invalid input");
    }

    else if(*worker_ptr == NULL)
    {
        log_error("worker_init: memory allocation failed: %s", strerror(errno));
    }

    else
    {
        /* Initialize the worker's pipe read file descriptor */
        (*worker_ptr)->pipe_read_fd = *pipe_read_fd;

        /* Initialize the worker's active sockets number */
        (*worker_ptr)->active_sockets_no = 0;

        /* Initialize the worker's state */
        (*worker_ptr)->status = SERVER_STATUS_ACTIVE;

        /* set return to success */
        res = STATUS_SUCCESS;
    }
    return res;
}

void *worker_run(void *arg)
{
    /* Check input */
    if(arg == NULL)
    {
        log_error("worker_run: invalid input");
        return NULL;
    }
    else
    {
        worker_t *worker_ptr = (worker_t *)arg;

        /* Array to hold epoll events */
        struct epoll_event events[MAX_CLIENTS];

        /* Create an epoll instance */
        int epfd = epoll_create1(0);

        if(epfd == -1)
        {
            log_error("worker_run: epoll_create1 failed: %s", strerror(errno));
            return NULL;
        }

        /* Prepare epoll_event for the pipe read end */
        struct epoll_event ev = {.events = EPOLLIN, .data.fd = worker_ptr->pipe_read_fd};

        /* Add the pipe read side to monitored sockets */
        if(epoll_ctl(epfd, EPOLL_CTL_ADD, worker_ptr->pipe_read_fd, &ev) == -1)
        {
            log_error("worker_run: epoll_ctl failed: %s", strerror(errno));
            close(epfd);
            return NULL;
        }

        /* Main worker thread loop */
        while(atomic_load(&worker_ptr->status) == SERVER_STATUS_ACTIVE)
        {
            // log_info("[worker] Sleeping in epoll_wait...");

            /* Wait for events on the pipe or client sockets */
            int nfds = epoll_wait(epfd, events, MAX_CLIENTS, -1);

            // log_info("[worker] Woke up with %d event(s)", nfds);

            /* Check for errors */
            if(nfds < 0)
            {
                log_error("worker_run: epoll_wait failed: %s", strerror(errno));
                continue;
            }

            /* Process all ready events */
            for(int i = 0; i < nfds; i++)
            {
                int fd = events[i].data.fd;

                /* If the event is on the pipe, read new client fds */
                if(fd == worker_ptr->pipe_read_fd)
                {
                    int client_fd;
                    /* Read all available client fds from the pipe */
                    while(read(worker_ptr->pipe_read_fd, &client_fd, sizeof(client_fd)) > 0)
                    {
                        /* Prepare epoll_event for the new client fd */
                        struct epoll_event cev = {.events = EPOLLIN, .data.fd = client_fd};

                        /* Add the new client fd to epoll */
                        if(epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &cev) == -1)
                        {
                            log_error("[worker] Failed to add client fd %d to epoll: %s", client_fd,
                                      strerror(errno));
                            close(client_fd);
                        }
                        else
                        {
                            log_info("[worker] Registered client fd %d to epoll", client_fd);
                        }
                    }
                }
                /* Otherwise, the event is on a client socket */
                else
                {
                    char recv_buf[HTTP_RECEIVE_BUFFER_LEN];
                    /* Read data from the client socket */
                    ssize_t n = read(fd, recv_buf, HTTP_RECEIVE_BUFFER_LEN - 1);

                    if(n <= 0)
                    {
                        /* Connection closed or error, clean up */
#ifdef DEBUG_MODE
                        log_info("[worker] Closing fd %d", fd);
#endif /* DEBUG_MODE */
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);
                    }
                    else
                    {
#ifdef DEBUG_MODE
                        // log_info("[worker] Received from fd %d:\n%.*s", fd, (int)n, recv_buf);
#endif /* DEBUG_MODE */

                        if(browser_manage_client_req(fd, recv_buf, n) != STATUS_SUCCESS)
                        {
#ifdef DEBUG_MODE
                            log_info("[worker] browser_manage_client_req failed fd %d", fd);
#endif /* DEBUG_MODE */
                            /* Close the socket and remove from epoll */
                            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                            close(fd);
                        }
                    }
                }
            }
        }

        /* Close the epoll file descriptor */
        close(epfd);
    }

    return NULL;
}

void worker_set_status(worker_t *worker_ptr, int status)
{
    if(worker_ptr == NULL)
    {
        log_error("listener_set_status: invalid listener pointer");
    }
    else
    {
        /* Set the status */
        atomic_store(&worker_ptr->status, status);

        /* Log the status change */
        log_info("Listener status set to %d", status);
    }
}
