/**
 * @file time_helper.h
 * @brief Linux timerfd helper declarations for server timers.
 *
 * This header exposes timerfd creation, timer arming, and current-time helpers used by worker/operator event loops.
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025-05-11
 */

#ifndef SERVER_TIMER_H
#define SERVER_TIMER_H

/*****************************************************************************************************************************************
 * PUBLIC INCLUDES
 *****************************************************************************************************************************************
 */
#include <stdint.h>

/*****************************************************************************************************************************************
 * PUBLIC STRUCTURED VARIABLES
 *****************************************************************************************************************************************
 */

/*****************************************************************************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 *****************************************************************************************************************************************
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
