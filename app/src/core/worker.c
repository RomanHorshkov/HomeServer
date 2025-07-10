/**
 * @file worker.c
 * @brief Manages client connections passed from the listener, handles requests, and handles
 * concurrency.
 *
 * This module waits for client file descriptors passed through a pipe, monitors them using epoll,
 * and delegates request processing to the browser subsystem. It gracefully closes client
 * connections on error or shutdown.
 *
 * Usage:
 *   worker_init(...)
 *   worker_run(...)
 *
 * Exit Codes:
 *   STATUS_SUCCESS  (0)
 *   STATUS_FAILURE  (1)
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑05‑11
 * (c) 2025
 */

#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include "worker.h" /* worker */

#include <fcntl.h>       /* fcntl(), F_GETFL, F_SETFL, O_NONBLOCK */
#include <netdb.h>       /* getaddrinfo(), addrinfo, gai_strerror() */
#include <stdatomic.h>   /* atomic_int */
#include <stdlib.h>      /* malloc(), calloc() etc */
#include <sys/epoll.h>   /* epoll_create1(), epoll_ctl(), epoll_wait(), struct epoll_event */
#include <sys/timerfd.h> /* timerfd_create(), struct itimerspec */
#include <unistd.h>      /* fork(), close(), read(), write(), etc. */

#include "browser.h"         /* browser_manage_client_req */
#include "logger.h"          /* logger */
#include "server_settings.h" /* settings */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */

/* This structure holds the worker's data, including epoll and timer instances, */
typedef struct
{
    int fd;
    time_t last_activity;
    int request_count;
} connection_t;

/* This structure holds the epoll file descriptor and the events array. */
typedef struct
{
    /* epoll instance */
    int epoll_fd;

    /* events array */
    struct epoll_event events[MAX_CLIENTS];

} epoll_var_t;

/* This structure holds the timer file descriptor and the timer specifications. */
typedef struct
{
    /* timer fd instance */
    int timer_fd;

    /* interval + initial expiry */
    struct itimerspec spec;

} timer_var_t;

/* This structure holds the worker's data, including epoll and timer instances,
 * connections, active connections count, pipe read file descriptor, and status.
 */
struct worker
{
    /* epoll instance */
    epoll_var_t epoll_vars;

    /* timer instance */
    timer_var_t timer_vars;

    /* connections */
    connection_t connections[MAX_CLIENTS];

    /* active connections */
    int active_connections_no;

    /* listener - worker pipe read fd */
    int pipe_read_fd;

    /* status variable */
    atomic_int status; /* 0 = inactive, 1 = active */
};

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

static int add_connection(worker_t *worker_ptr);

static int delete_connection(worker_t *worker_ptr, int fd);

static int update_connection(worker_t *worker_ptr, int fd);

static int worker_timer_init(timer_var_t *tv, int epfd);

void worker_timer_update(worker_t *worker_ptr);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int worker_init(worker_t **worker_ptr, const int *pipe_read_fd)
{
#ifdef DEBUG_MODE
    log_info("Starting Worker...");
#endif /* DEBUG_MODE */

    /* return value */
    int res = STATUS_FAILURE;

    /* Allocate memory for the worker */
    *worker_ptr = (worker_t *)calloc(1, sizeof(worker_t));

    /* Create an epoll instance */
    int epoll_fd = epoll_create1(0);

    /* Check input */
    if(pipe_read_fd == NULL)
    {
        log_error("[worker]:worker_init: invalid input");
    }

    /* check memory allocation */
    else if(*worker_ptr == NULL)
    {
        log_error("[worker]:worker_init: memory allocation failed: %s", strerror(errno));
    }

    /* check epoll instance */
    else if(epoll_fd == -1)
    {
        log_error("[worker]: worker_init: epoll_create1 failed: %s", strerror(errno));
    }

    else
    {
        /* Initialize the worker's epoll instance */
        (*worker_ptr)->epoll_vars.epoll_fd = epoll_fd;

        /* Initialize the worker's connections(_t) */
        memset((*worker_ptr)->connections, 0, MAX_CLIENTS * sizeof(connection_t));

        /* Initialize the worker's pipe read file descriptor */
        (*worker_ptr)->pipe_read_fd = *pipe_read_fd;

        /* Initialize the worker's active sockets number */
        (*worker_ptr)->active_connections_no = 0;

        /* Initialize the worker's state */
        atomic_store(&(*worker_ptr)->status, SERVER_STATUS_ACTIVE);

        /* init the timer */
        worker_timer_init(&(*worker_ptr)->timer_vars, (*worker_ptr)->epoll_vars.epoll_fd);

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
        log_error("[worker]: worker_run: invalid input");
        return NULL;
    }

    /* make a local copy of the worker ptr */
    worker_t *worker_ptr = (worker_t *)arg;

    /* Prepare epoll_event for the pipe read end */
    struct epoll_event ev_pipe = {.events = EPOLLIN, .data.fd = worker_ptr->pipe_read_fd};

    /* Add the pipe read side to monitored sockets */
    if(epoll_ctl(worker_ptr->epoll_vars.epoll_fd, EPOLL_CTL_ADD, worker_ptr->pipe_read_fd,
                 &ev_pipe) == -1)
    {
        log_error("[worker]: worker_run: epoll_ctl failed on pipe: %s", strerror(errno));
        close(worker_ptr->epoll_vars.epoll_fd);
        return NULL;
    }

    /* Main worker thread loop */
    while(atomic_load(&worker_ptr->status) == SERVER_STATUS_ACTIVE)
    {
        /* Wait for events on the pipe or client sockets */
        /* Here the worker stops if there's nothing to do */
        int nfds = epoll_wait(worker_ptr->epoll_vars.epoll_fd, worker_ptr->epoll_vars.events,
                              MAX_CLIENTS, -1);

        /* Check for errors */
        if(nfds < 0)
        {
            log_error("[worker]: worker_run: epoll_wait failed: %s", strerror(errno));
            continue;
        }

        /* Process all ready events */
        for(int i = 0; i < nfds; i++)
        {
            int fd = worker_ptr->epoll_vars.events[i].data.fd;
            uint32_t ev_conn = worker_ptr->epoll_vars.events[i].events;

            /* Handle error/hangup/peer close events */
            if(ev_conn & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
#ifdef DEBUG_MODE
                log_info("[worker] epoll event: fd %d closed/hup/err (events=0x%x)", fd, ev_conn);
#endif
                delete_connection(worker_ptr, fd);
                continue;
            }

            /* If the event is on the timer handle logic */
            if(fd == worker_ptr->timer_vars.timer_fd)
            {
#ifdef DEBUG_MODE
                log_info("[worker] Timer event received");
#endif /* DEBUG_MODE */
                uint64_t expirations;
                ssize_t s = read(fd, &expirations, sizeof(expirations));
                if(s != sizeof(expirations))
                {
                    log_error("[worker] Failed to read timerfd: %s", strerror(errno));
                }

                /* Update the timer */
                worker_timer_update(worker_ptr);
                continue;  // Prevent further processing of this fd
            }

            /* If the event is on the pipe add a new connection */
            else if(fd == worker_ptr->pipe_read_fd)
            {
                if(add_connection(worker_ptr) != STATUS_SUCCESS)
                {
                    log_error("[worker] Failed to add connection for fd %d", fd);
                    delete_connection(worker_ptr, fd);
                }
                else
                {
#ifdef DEBUG_MODE
                    log_info("[worker] New connection added from pipe, fd %d", fd);
#endif /* DEBUG_MODE */
                }
                continue;  // Prevent further processing of this fd
            }

            /* Otherwise, the event is on a client socket */
            else if(browser_manage_client_req(fd) != STATUS_SUCCESS)
            {
#ifdef DEBUG_MODE
                log_error("[worker]: browser failed fd %d, closing it.", fd);
#endif /* DEBUG_MODE */
                delete_connection(worker_ptr, fd);
                continue;  // Prevent further processing of this fd
            }

            /* Refresh connection's request_count and last_activity */
            else if(update_connection(worker_ptr, fd) != STATUS_SUCCESS)
            {
                log_error("[worker] update_connection failed for fd %d", fd);
                delete_connection(worker_ptr, fd);
                continue;  // Prevent further processing of this fd
            }
        }
    } /* while(SERVER_STATUS_ACTIVE) */

    /* Close the epoll file descriptor */
    close(worker_ptr->epoll_vars.epoll_fd);

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

static int add_connection(worker_t *worker_ptr)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Client file descriptor read from the pipe */
    int client_fd;

    /* Check input */
    if(worker_ptr == NULL)
    {
        log_error("[worker] add_connection: invalid input");
    }

    /* Check total connections available */
    else if(worker_ptr->active_connections_no >= MAX_CLIENTS)
    {
        log_error("[worker] add_connection: Maximum number of connections reached: %d",
                  MAX_CLIENTS);
    }

    /* Check if slot is free */
    else if(worker_ptr->connections[worker_ptr->active_connections_no].fd != 0)
    {
        log_error("[worker] add_connection: No free slot at %d, busy by fd ",
                  worker_ptr->active_connections_no,
                  worker_ptr->connections[worker_ptr->active_connections_no].fd);
    }

    else
    {
        /* Drain every fd the listener pushed into the pipe */
        while(read(worker_ptr->pipe_read_fd, &client_fd, sizeof(client_fd)) > 0)
        {
            /* Find a free slot */
            int slot = -1;
            for(int i = 0; i < MAX_CLIENTS; ++i)
            {
                if(worker_ptr->connections[i].fd == 0)
                {
                    slot = i;
                    break;
                }
            }

            if(slot == -1)
            {
                log_error("[worker] add_connection: slot now found dropping fd %d", client_fd);
                continue;
            }

            /* Prepare epoll_event for the new client fd */
            struct epoll_event cev = {.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR,
                                      .data.fd = client_fd};

            /* Add the new client fd to epoll */
            if(epoll_ctl(worker_ptr->epoll_vars.epoll_fd, EPOLL_CTL_ADD, client_fd, &cev) == -1)
            {
                log_error("[worker] Failed to add client fd %d to epoll: %s", client_fd,
                          strerror(errno));
            }

            /* Initialise bookkeeping */
            else
            {
                /* set the connection's details */
                connection_t *c = &worker_ptr->connections[slot];
                c->fd = client_fd;
                c->last_activity = time(NULL);
                c->request_count = 0;

                worker_ptr->active_connections_no++;

                /* set return to success */
                res = STATUS_SUCCESS;
#ifdef DEBUG_MODE
                log_info("[worker] Registered client fd %d to epoll", client_fd);
#endif /* DEBUG_MODE */
            }
        }
    }

    return res;
}

static int delete_connection(worker_t *worker_ptr, int fd)
{
    int res = STATUS_FAILURE;
    if(worker_ptr == NULL)
    {
        log_error("[worker] delete_connection: invalid input");
    }
    else
    {
        for(int j = 0; j < MAX_CLIENTS; ++j)
        {
            if(worker_ptr->connections[j].fd == fd)
            {
                /* send FIN – graceful half-close */
                shutdown(fd, SHUT_WR);

                /* Remove from epoll */
                epoll_ctl(worker_ptr->epoll_vars.epoll_fd, EPOLL_CTL_DEL, fd, NULL);

                /* Close the socket */
                close(fd);

                /* Reset the connection slot */
                worker_ptr->connections[j].fd = 0;
                worker_ptr->connections[j].last_activity = 0;
                worker_ptr->connections[j].request_count = 0;
                worker_ptr->active_connections_no--;

                res = STATUS_SUCCESS;
#ifdef DEBUG_MODE
                log_info("[worker] delete_connection: fd %d removed and slot reset", fd);
#endif
                break;
            }
        }
    }
    return res;
}

static int update_connection(worker_t *worker_ptr, int fd)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(worker_ptr == NULL)
    {
        log_error("[worker] update_connection: invalid input");
    }

    else
    {
        /* TO DO : HASH TABLE */
        /* Find the connection and update it */
        for(int j = 0; j < MAX_CLIENTS; ++j)
        {
            if(worker_ptr->connections[j].fd == fd)
            {
                worker_ptr->connections[j].last_activity = time(NULL);
                worker_ptr->connections[j].request_count++;
                res = STATUS_SUCCESS;
                break;
            }
        }
    }

    return res;
}

static int worker_timer_init(timer_var_t *tv, int epfd)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(tv == NULL)
    {
        log_error("[worker] worker_timer_init: invalid input");
    }
    else
    {
        /* Create a timer fd */
        tv->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if(tv->timer_fd == -1)
        {
            log_error("[worker] worker_timer_init: timerfd_create failed: %s", strerror(errno));
        }
        else
        {
            /* Set the timer interval and initial expiry */
            tv->spec.it_value.tv_sec =
                SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE; /* Initial expiry in seconds */
            tv->spec.it_value.tv_nsec = 0;
            tv->spec.it_interval.tv_sec =
                SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE; /* Interval in seconds */
            tv->spec.it_interval.tv_nsec = 0;

            /* Set the timer */
            if(timerfd_settime(tv->timer_fd, 0, &tv->spec, NULL) == -1)
            {
                log_error("[worker] worker_timer_init: timerfd_settime failed: %s",
                          strerror(errno));
                close(tv->timer_fd);
            }
            else
            {
                /* Prepare epoll_event for the timer fd */
                struct epoll_event tev = {.events = EPOLLIN, .data.fd = tv->timer_fd};

                /* Add the timer fd to epoll */
                if(epoll_ctl(epfd, EPOLL_CTL_ADD, tv->timer_fd, &tev) == -1)
                {
                    log_error("[worker] worker_timer_init: epoll_ctl failed on timer fd: %s",
                              strerror(errno));
                    close(tv->timer_fd);
                }
                else
                {
                    /* Set return to success */
                    res = STATUS_SUCCESS;
                }
            }
        }
    }

    return res;
}

void worker_timer_update(worker_t *worker_ptr)
{
    if(worker_ptr == NULL) return;

    timer_var_t *tv = &worker_ptr->timer_vars;

    /* Set timer interval based on active connections */
    if(worker_ptr->active_connections_no == 0 &&
       tv->spec.it_value.tv_sec != SERVER_KEEPALIVE_TIMEOUT_ALONE)
    {
        tv->spec.it_value.tv_sec = SERVER_KEEPALIVE_TIMEOUT_ALONE;
        tv->spec.it_interval.tv_sec = SERVER_KEEPALIVE_TIMEOUT_ALONE;
#ifdef DEBUG_MODE
        log_info(
            "[worker] worker_timer_update: Updated timer to SERVER_KEEPALIVE_TIMEOUT_ALONE (%lu "
            "seconds)",
            SERVER_KEEPALIVE_TIMEOUT_ALONE);
#endif /* DEBUG_MODE */
    }
    else if(worker_ptr->active_connections_no > 0 &&
            tv->spec.it_value.tv_sec != SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE)
    {
        tv->spec.it_value.tv_sec = SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE;
        tv->spec.it_interval.tv_sec = SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE;
#ifdef DEBUG_MODE
        log_info(
            "[worker] worker_timer_update: Updated timer to SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE "
            "(%lu seconds)",
            SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE);
#endif /* DEBUG_MODE */
    }
    // tv->spec.it_value.tv_nsec = 0;
    // tv->spec.it_interval.tv_nsec = 0;

    /* Update the timer */
    if(timerfd_settime(tv->timer_fd, 0, &tv->spec, NULL) == -1)
    {
        log_error("[worker] worker_timer_update: timerfd_settime failed: %s", strerror(errno));
    }
}
