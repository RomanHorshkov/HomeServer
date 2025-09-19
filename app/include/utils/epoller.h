/**
 * @file epoll_helper.h
 * @brief Thin wrapper around Linux **timerfd** providing an epoll‑friendly,
 *        millisecond‑granularity timer object.
 *
 * The API intentionally hides the underlying file descriptor so that callers
 * cannot misuse it (e.g. by closing it twice).  All functions are
 * signal‑safe and allocate no additional heap memory after @ref timer_init().
 *
 * Typical usage pattern:
 * @code
 * timer_t *t = timer_init(500);           // 500 ms periodic
 * epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd(t), &(struct epoll_event){ .events = EPOLLIN });
 *
 * ... in the event loop ...
 * if (ev.data.fd == timer_fd(t)) {
 *     while (timer_drain(t) > 0)           // drain every expiration
 *         do_something();
 * }
 *
 * timer_destroy(t);
 * @endcode
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025-05-11
 */

#ifndef SERVER_EPOLLER_H
#define SERVER_EPOLLER_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */
#include <stdint.h>

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES
 ****************************************************************************
 */

struct epoll_event;

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/** Create a new epoll instance.
 *  @return  ≥0 epoll fd, <0 = -errno */
int epoller_new(void);

/** Wait for events.
 *  @param epoll_fd     epoll instance fd
 *  @param ev_out       buffer to receive events
 *  @return  ≥0 number of events, <0 = -errno */
int epoller_wait(int epoll_fd, struct epoll_event *out_events);

/** Add, modify, or delete a file descriptor in the epoll set.
 *  @param epoll_fd  epoll instance fd
 *  @param op        one of EPOLL_CTL_ADD / EPOLL_CTL_MOD / EPOLL_CTL_DEL
 *  @param fd        the target file descriptor
 *  @param event     event mask (only used for ADD and MOD; ignored for DEL)
 *  @param data      ptr to user data to store in the kernel tree
 *  @return  0 on success, <0 = -errno on failure */
int epoller_manage_fd(int epoll_fd, int target_fd, int operation,
                      uint32_t event, void *data);

int epoller_manage_fd_with_ptr(int epoll_fd, int target_fd, void *target_ptr,
                               int operation, uint32_t events);

/** Check whether an epoll event mask indicates we should close the connection */
int epoller_check_if_to_close(uint32_t ev_conn);

/** Close down the epoll instance.
 *  @return  0 on success, <0 = -errno on failure */
int epoller_shutdown(int epoll_fd);

#endif /* SERVER_EPOLLER_H */
