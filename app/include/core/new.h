
// /*
//  * conn_mgr.h
//  * Manages active client connections and timeouts
//  */
// #ifndef CONN_MGR_H
// #    define CONN_MGR_H

// #    include <stdint.h>

// // Opaque connection manager
// typedef struct conn_mgr conn_mgr_t;

// typedef void (*idle_cb)(int fd, void *ctx);

// int conn_mgr_init(conn_mgr_t **m);
// int conn_mgr_shutdown(conn_mgr_t *m);

// // Track connection
// int conn_mgr_add(conn_mgr_t *m, int fd);
// int conn_mgr_remove(conn_mgr_t *m, int fd);
// int conn_mgr_refresh(conn_mgr_t *m, int fd);

// // Iterate and close idle connections older than timeout_secs
// int conn_mgr_cleanup_idle(conn_mgr_t *m, uint32_t timeout_secs, idle_cb cb, void *ctx);

// #endif  // CONN_MGR_H

// /*
//  * conn_mgr.c
//  */
// #include <stdlib.h>
// #include <time.h>
// #include <unistd.h>

// #include "conn_mgr.h"

// struct conn
// {
//     int fd;
//     time_t last_active;
// };

// struct conn_mgr
// {
//     struct conn *list;
//     size_t cap;
//     size_t len;
// };

// int conn_mgr_init(conn_mgr_t **m_out)
// {
//     if(!m_out) return -1;
//     conn_mgr_t *m = calloc(1, sizeof(*m));
//     if(!m) return -1;
//     m->cap = 128;
//     m->list = calloc(m->cap, sizeof(*m->list));
//     if(!m->list)
//     {
//         free(m);
//         return -1;
//     }
//     *m_out = m;
//     return 0;
// }

// int conn_mgr_shutdown(conn_mgr_t *m)
// {
//     if(!m) return -1;
//     free(m->list);
//     free(m);
//     return 0;
// }

// static ssize_t find_idx(conn_mgr_t *m, int fd)
// {
//     for(size_t i = 0; i < m->len; i++)
//         if(m->list[i].fd == fd) return i;
//     return -1;
// }

// int conn_mgr_add(conn_mgr_t *m, int fd)
// {
//     if(!m) return -1;
//     if(m->len == m->cap)
//     {
//         size_t newcap = m->cap * 2;
//         struct conn *tmp = realloc(m->list, newcap * sizeof(*tmp));
//         if(!tmp) return -1;
//         m->list = tmp;
//         m->cap = newcap;
//     }
//     m->list[m->len++] = (struct conn){.fd = fd, .last_active = time(NULL)};
//     return 0;
// }

// int conn_mgr_remove(conn_mgr_t *m, int fd)
// {
//     if(!m) return -1;
//     ssize_t idx = find_idx(m, fd);
//     if(idx < 0) return -1;
//     m->list[idx] = m->list[--m->len];
//     return 0;
// }

// int conn_mgr_refresh(conn_mgr_t *m, int fd)
// {
//     if(!m) return -1;
//     ssize_t idx = find_idx(m, fd);
//     if(idx < 0) return -1;
//     m->list[idx].last_active = time(NULL);
//     return 0;
// }

// int conn_mgr_cleanup_idle(conn_mgr_t *m, uint32_t timeout_secs, idle_cb cb, void *ctx)
// {
//     if(!m || !cb) return -1;
//     time_t now = time(NULL);
//     for(size_t i = 0; i < m->len;)
//     {
//         if((now - m->list[i].last_active) > timeout_secs)
//         {
//             cb(m->list[i].fd, ctx);
//             m->list[i] = m->list[--m->len];
//         }
//         else
//         {
//             i++;
//         }
//     }
//     return 0;
// }

// /*
//  * timerfd_wrapper.h
//  * Thin wrapper around timerfd for periodic tasks
//  */
// #ifndef TIMERFD_WRAPPER_H
// #    define TIMERFD_WRAPPER_H

// int timerfd_init_periodic(uint32_t initial_sec, uint32_t interval_sec);
// int timerfd_drain(int fd);

// #endif  // TIMERFD_WRAPPER_H

// /*
//  * timerfd_wrapper.c
//  */
// #include <stdint.h>
// #include <sys/timerfd.h>
// #include <unistd.h>

// #include "timerfd_wrapper.h"

// int timerfd_init_periodic(uint32_t initial_sec, uint32_t interval_sec)
// {
//     int fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
//     if(fd < 0) return -1;
//     struct itimerspec spec = {.it_value.tv_sec = initial_sec, .it_interval.tv_sec =
//     interval_sec}; if(timerfd_settime(fd, 0, &spec, NULL) < 0)
//     {
//         close(fd);
//         return -1;
//     }
//     return fd;
// }

// int timerfd_drain(int fd)
// {
//     uint64_t exp;
//     return read(fd, &exp, sizeof(exp)) < 0 ? -1 : 0;
// }

// /*
//  * pipeline.h
//  * Unified pipe + eventfd + ring buffer API
//  */
// #ifndef PIPELINE_H
// #    define PIPELINE_H

// #    include <stdint.h>

// #    include "spsc_ring.h"

// typedef struct pipeline pipeline_t;

// int pipeline_init(pipeline_t **p_out);
// int pipeline_shutdown(pipeline_t *p);

// int pipeline_push(pipeline_t *p, int fd);
// int pipeline_pop(pipeline_t *p, int *fd_out);

// int pipeline_notify_status(pipeline_t *p, uint32_t status);
// uint32_t pipeline_read_status(pipeline_t *p);

// int pipeline_wakeup_fd(const pipeline_t *p);
// int pipeline_pipe_read_fd(const pipeline_t *p);

// #endif  // PIPELINE_H

// /*
//  * pipeline.c
//  */
// #include <errno.h>
// #include <stdlib.h>
// #include <sys/eventfd.h>
// #include <unistd.h>

// #include "pipeline.h"

// struct pipeline
// {
//     int pipe_fds[2];
//     int wakeup_fd;
//     spsc_ring_t *ring;
// };

// int pipeline_init(pipeline_t **p_out)
// {
//     if(!p_out) return -1;
//     pipeline_t *p = calloc(1, sizeof(*p));
//     if(!p) return -1;
//     if(pipe(p->pipe_fds) < 0) goto err;
//     // make non-blocking omitted for brevity
//     p->wakeup_fd = eventfd(0, EFD_NONBLOCK);
//     if(p->wakeup_fd < 0) goto err;
//     p->ring = spsc_ring_init(1024);
//     if(!p->ring) goto err;
//     *p_out = p;
//     return 0;
// err:
//     close(p->pipe_fds[0]);
//     close(p->pipe_fds[1]);
//     if(p->wakeup_fd > 0) close(p->wakeup_fd);
//     free(p);
//     return -1;
// }

// int pipeline_push(pipeline_t *p, int fd)
// {
//     if(spsc_ring_full(p->ring)) return -1;
//     if(spsc_ring_push(p->ring, fd)) return -1;
//     uint64_t v = 1;
//     write(p->wakeup_fd, &v, sizeof(v));
//     return 0;
// }

// int pipeline_pop(pipeline_t *p, int *fd_out)
// {
//     if(spsc_ring_empty(p->ring)) return 0;
//     if(spsc_ring_pop(p->ring, fd_out) < 0) return -1;
//     return 1;
// }

// int pipeline_notify_status(pipeline_t *p, uint32_t status)
// {
//     return write(p->pipe_fds[1], &status, sizeof(status)) == sizeof(status) ? 0 : -1;
// }

// uint32_t pipeline_read_status(pipeline_t *p)
// {
//     uint32_t s;
//     if(read(p->pipe_fds[0], &s, sizeof(s)) != sizeof(s)) return UINT32_MAX;
//     return s;
// }

// int pipeline_wakeup_fd(const pipeline_t *p)
// {
//     return p->wakeup_fd;
// }
// int pipeline_pipe_read_fd(const pipeline_t *p)
// {
//     return p->pipe_fds[0];
// }
