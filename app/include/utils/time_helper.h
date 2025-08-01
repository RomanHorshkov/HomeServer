/**
 * @file timer.h
 * @brief Thin wrapper around Linux **timerfd** providing an epoll‑friendly,
 *        millisecond‑granularity timer object.
 *
 * The API intentionally hides the underlying file descriptor so that callers
 * cannot misuse it (e.g. by closing it twice).  All functions are
 * signal‑safe and allocate no additional heap memory after @ref time_helper_init().
 *
 * Typical usage pattern:
 * @code
 * timer_t *t = time_helper_init(500);           // 500 ms periodic
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

#ifndef SERVER_TIMER_H
#define SERVER_TIMER_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */
#include <stdint.h>

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES
 ****************************************************************************
 */

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief Create a one‑shot or periodic timer.
 *
 * @param period_ms  Desired period in **milliseconds**.
 *                   - `period_ms == 0` → timer is created *disarmed* (one‑shot mode; caller
 *                     must arm it later with @ref time_helper_set())
 *                   - `period_ms  > 0` → periodic mode with the given interval.
 *
 * @return Pointer to an initialised timer object on success, or `NULL` on error
 *         (with `errno` set by the underlying syscall).
 *
 * @note The timer uses **CLOCK_MONOTONIC** so it is not affected by NTP or
 *       manual clock adjustments.
 */
int time_helper_init(void);

/**
 * @brief Arm, re‑arm or disarm a timer.
 *
 * @param t          Timer handle.
 * @param period_ms  New period in milliseconds.
 *                   - `0` → disarm (timer stops until armed again).
 *                   - `>0` → periodic interval; a one‑shot timer becomes periodic.
 *
 * @retval  0  Success.
 * @retval -1  Failure (`errno` will be set as per `timerfd_settime(2)`).
 *
 * @warning Calling this from multiple threads without external synchronisation
 *          results in a data race.
 */
int time_helper_set(int timer_fd, uint32_t s, uint32_t ns);

int time_helper_get_now(void);

#endif /* SERVER_TIMER_H */
