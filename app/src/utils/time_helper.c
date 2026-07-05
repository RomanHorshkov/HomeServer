/**
 * @file time_helper.c
 * @brief Linux timerfd helper implementation for monotonic server timers.
 */

#ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200112L
#endif
#define _GNU_SOURCE
#include "time_helper.h"

#include <sys/time.h>
#include <sys/timerfd.h> /* timerfd_create(), struct itimerspec */

int time_helper_init(void)
{
    return timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
}

int time_helper_set(int timer_fd, uint32_t s, uint32_t ns)
{
    struct itimerspec spec = {.it_value.tv_sec = s, .it_value.tv_nsec = ns, .it_interval.tv_sec = s, .it_interval.tv_nsec = ns};
    return timerfd_settime(timer_fd, 0, &spec, NULL);
}

int time_helper_get_now(void)
{
    return (int)time(NULL);
}
