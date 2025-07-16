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

typedef _Atomic worker_status atomic_worker_status_t;

typedef struct
{
    /* connection's file descriptor */
    int fd;

    /* last activity timestamp */
    time_t last_activity;

    /* number of requests handled */
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

    /* collaboration with listener structure */
    pipeline_t *pipeline;

    /* status variable */
    atomic_worker_status_t status; /* 0 = inactive, 1 = active */
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

static worker_status add_connections(worker_t *worker_ptr);

static int delete_connection(worker_t *worker_ptr, int fd);

static int update_connection(worker_t *worker_ptr, int fd);

static int worker_timer_init(timer_var_t *tv, int epfd);

static int manage_time_event(worker_t *worker_ptr, int fd);

static void worker_timer_update(worker_t *worker_ptr);

static int send_status_to_listener(worker_t *worker_ptr, worker_status status);

static int worker_set_status_and_update_listener(worker_t *worker_ptr, worker_status status);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int worker_init(worker_t **worker_ptr, pipeline_t *pipeline_ptr)
{
    /* return value */
    int res = STATUS_FAILURE;

    /* Check input */
    if(worker_ptr == NULL || pipeline_ptr == NULL)
    {
        log_error("[worker]:worker_init: invalid input");
    }
    else
    {
        /* Allocate memory for the worker */
        *worker_ptr = (worker_t *)calloc(1, sizeof(worker_t));

        /* Create an epoll instance */
        int epoll_fd = epoll_create1(0);

        /* Check memory allocation */
        if(*worker_ptr == NULL)
        {
            log_error("[worker]:worker_init: memory allocation failed: %s", strerror(errno));
        }

        /* Check epoll instance */
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

            /* Initialize the worker's active sockets number */
            (*worker_ptr)->active_connections_no = 0;

            /* Initialize the worker's communication pipeline */
            (*worker_ptr)->pipeline = pipeline_ptr;

            // /** Register the pipe for EPOLLOUT events add oneshot flag to avoid epoll wakeup at
            // every
            //  * cicle this is needed to avoid the worker thread being woken up every time the pipe
            //  is
            //  * ready to be written.
            //  */
            // struct epoll_event ev_pipe = {.events = EPOLLOUT | EPOLLONESHOT,
            //                               .data.fd = pipeline_ptr->pipe_fds[1]};

            // /* Add the pipe write side to monitored sockets */
            // if(epoll_ctl((*worker_ptr)->epoll_vars.epoll_fd, EPOLL_CTL_ADD,
            //                   (*worker_ptr)->pipeline->pipe_fds[1], &ev_pipe) == -1)
            // {
            //     log_error("[worker]: worker_run: epoll_ctl failed on pipe: %s", strerror(errno));
            // }

            /* Register wakeup_fd into epoll */
            struct epoll_event ev_wakeup = {.events = EPOLLIN, .data.fd = pipeline_ptr->wakeup_fd};
            if(epoll_ctl((*worker_ptr)->epoll_vars.epoll_fd, EPOLL_CTL_ADD, pipeline_ptr->wakeup_fd,
                         &ev_wakeup) == -1)
            {
                log_error("[worker]: worker_run: epoll_ctl failed on wakeup_fd: %s", strerror(errno));
            }

            /* init the timer */
            else if(worker_timer_init(&(*worker_ptr)->timer_vars,
                                      (*worker_ptr)->epoll_vars.epoll_fd) != STATUS_SUCCESS)
            {
                log_error("[worker]: worker_init: timer_init failed: %s", strerror(errno));
            }

            else
            {
                /* Initialize the worker's state */
                atomic_store(&(*worker_ptr)->status, WORKER_STATUS_ACTIVE);

                /* set return to success */
                res = STATUS_SUCCESS;
            }
        }
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

    worker_status worker_old_status = WORKER_STATUS_ACTIVE;
    worker_status worker_new_status = WORKER_STATUS_ACTIVE;

    worker_set_status_and_update_listener(worker_ptr, worker_old_status);

    /* Main worker thread loop */
    while(worker_old_status == WORKER_STATUS_ACTIVE || worker_old_status == WORKER_STATUS_FULL)
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

        /* Update the worker's new status */
        worker_new_status = atomic_load(&worker_ptr->status);

        /* Process all ready events */
        for(int i = 0; i < nfds; i++)
        {
            int fd = worker_ptr->epoll_vars.events[i].data.fd;
            uint32_t ev_conn = worker_ptr->epoll_vars.events[i].events;

            /* Event is on the timer handle logic */
            if(fd == worker_ptr->timer_vars.timer_fd)
            {
                manage_time_event(worker_ptr, fd);
                /* Prevent further processing of this fd */
                continue;
            }

            /* Event on the write pipe */
            else if(fd == worker_ptr->pipeline->pipe_fds[1])
            {
#ifdef DEBUG_MODE
                log_info("[worker] pipe read side is ready to write");
#endif /* DEBUG_MODE */
                continue;
            }

            /* Event is on the wakeup_fd coming from the listener, stands for time to check the ring
             * for new clients */
            else if(fd == worker_ptr->pipeline->wakeup_fd)
            {
                /* Clean the wakeup_fd with read and proceed with ring reading */
                uint64_t wakeup_count;
                ssize_t s = read(fd, &wakeup_count, sizeof(wakeup_count));
#ifdef DEBUG_MODE
                log_info("[worker] Wakeup event received on fd %d, wakeup_count %ld, size %ld", fd,
                         wakeup_count, s);
#endif /* DEBUG_MODE */

                worker_new_status = add_connections(worker_ptr);

                switch(worker_new_status)
                {
                    case WORKER_STATUS_ACTIVE:

                        /* Do nothing, keep going */
                        break;

                    case WORKER_STATUS_FULL:

                        /* Do nothing, keep going */
                        break;

                    case WORKER_STATUS_INACTIVE:
                    case WORKER_STATUS_INVALID:
                    default:

                        log_error("[worker] Failed to read and add connection from ring (fd %d)",
                                  fd);
                        delete_connection(worker_ptr, fd);
                        break;
                }
                continue;  // Prevent further processing of this fd
            }

            /* Otherwise, the event is on a client socket */

            /* Handle error/hangup/peer close events */
            else if(ev_conn & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
#ifdef DEBUG_MODE
                log_info("[worker] epoll event: fd %d closed/hup/err (events=0x%x)", fd, ev_conn);
#endif
                delete_connection(worker_ptr, fd);
                continue;
            }

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

            /* Check if the worker's status had changed */
            if(worker_old_status != worker_new_status)
            {
                worker_set_status_and_update_listener(worker_ptr, worker_new_status);
                /* Update the old status */
                worker_old_status = worker_new_status;
            }
        }
    } /* while(SERVER_STATUS_ACTIVE) */

    /* Close the epoll file descriptor */
    close(worker_ptr->epoll_vars.epoll_fd);

    return NULL;
}

int worker_set_status(worker_t *worker_ptr, worker_status status)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    if(worker_ptr == NULL)
    {
        log_error("[worker] worker_set_status: invalid listener pointer");
    }

    else if(status >= WORKER_STATUS_INVALID)
    {
        log_error("[worker] worker_set_status_and_update_listener: invalid status value %d",
                  status);
    }

    else
    {
        /* Set the status */
        atomic_store(&worker_ptr->status, status);

        res = STATUS_SUCCESS;

#ifdef DEBUG_MODE
        /* Log the status change */
        log_info("[worker] status set to %d", status);
#endif /* DEBUG_MODE */
    }

    return res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static worker_status add_connections(worker_t *worker_ptr)
{
    /* Return variable */
    worker_status res = WORKER_STATUS_INVALID;

    /* Check input */
    if(worker_ptr == NULL || worker_ptr->pipeline == NULL || worker_ptr->pipeline->ring_ptr == NULL)
    {
        log_error("[worker] add_connection: invalid input");
    }

    else if(worker_ptr->active_connections_no < MAX_CLIENTS)
    {
        /* Client file descriptor to read from the ring */
        int client_fd;

        /* Drain every fd the listener pushed into the ring */
        while(!spsc_ring_is_empty(worker_ptr->pipeline->ring_ptr) &&
              spsc_ring_pop(worker_ptr->pipeline->ring_ptr, &client_fd) != -1)
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

            /* Check if a free slot was found */
            if(slot == -1)
            {
                log_error("[worker] add_connection: slot not found for fd %d", client_fd);
                /* no free slot, something quite wrong happened here */
                break;
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

                /* set connection's client fd */
                c->fd = client_fd;
                /* set connection's last activity to now */
                c->last_activity = time(NULL);
                /* set connection's request count to 0 */
                c->request_count = 0;

                worker_ptr->active_connections_no++;

                /* set return to success */
                res = WORKER_STATUS_ACTIVE;
#ifdef DEBUG_MODE
                log_info("[worker] add_connection: Registered client fd %d to epoll", client_fd);
#endif /* DEBUG_MODE */
            }

            /* Check the worker's status about max connections allowed */
            if(worker_ptr->active_connections_no >= MAX_CLIENTS)
            {
#ifdef DEBUG_MODE
                log_info("[worker] add_connection: Maximum number of connections reached: %d",
                         MAX_CLIENTS);
#endif /* DEBUG_MODE */
                res = WORKER_STATUS_FULL;

                /* Exit the loop if max connections reached */
                break;
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
    log_info("Initializing worker_timer_init");
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

    log_info("Initialized worker_timer_init");
    return res;
}

static int manage_time_event(worker_t *worker_ptr, int fd)
{
    /* Result variable */
    int res = STATUS_FAILURE;

#ifdef DEBUG_MODE
    log_info("[worker] Timer event received");
#endif /* DEBUG_MODE */

    /* Read expirations */
    uint64_t expirations;
    ssize_t s = read(fd, &expirations, sizeof(expirations));
    if(s != sizeof(expirations))
    {
        log_error("[worker] Failed to read timerfd: %s", strerror(errno));
    }
    else
    {
        /* Update the timer */
        worker_timer_update(worker_ptr);

        res = STATUS_SUCCESS;
    }

    return res;
}

static void worker_timer_update(worker_t *worker_ptr)
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

static int send_status_to_listener(worker_t *worker_ptr, worker_status status)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(worker_ptr == NULL || worker_ptr->pipeline == NULL)
    {
        log_error("[worker] send_status_to_listener: invalid input");
    }

    /* Send the status to the listener */
    else if(write(worker_ptr->pipeline->pipe_fds[1], &status, sizeof(uint32_t)) != sizeof(uint32_t))
    {
        log_error("[worker] send_status_to_listener: write failed: %s", strerror(errno));
    }

    /* If everything went ok */
    else
    {
        res = STATUS_SUCCESS;
    }

    return res;
}

static int worker_set_status_and_update_listener(worker_t *worker_ptr, worker_status status)
{
    /* Result variable */
    int res = STATUS_FAILURE;

    /* Check input */
    if(worker_ptr == NULL)
    {
        log_error("worker_set_status_and_update_listener: invalid worker pointer");
    }

    else if(send_status_to_listener(worker_ptr, status) != STATUS_SUCCESS)
    {
        log_error("worker_set_status_and_update_listener: send_status_to_listener failed");
    }

    else if(worker_set_status(worker_ptr, status) != STATUS_SUCCESS)
    {
        log_error("worker_set_status_and_update_listener: worker_set_status failed");
    }

    else
    {
#ifdef DEBUG_MODE
        log_info("[worker] worker_set_status_and_update_listener: Worker status set to %d",
                 (int)status);
#endif /* DEBUG_MODE */

        res = STATUS_SUCCESS;
    }

    return res;
}
