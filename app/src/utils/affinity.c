/**
 * @file affinity.c
 *
 * @brief CPU affinity pinning implementation (see affinity.h).
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    jul 2026
 * (c) 2026
 */

#ifndef _GNU_SOURCE
#    define _GNU_SOURCE /* CPU_SET macros, pthread_setaffinity_np, sched_getaffinity */
#endif

#include <db_server/utils/affinity.h>

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>

#include <emlog.h>

#define LOG_TAG "srv_affinity"

/**
 * @brief Enumerate the process's allowed CPUs into out[]; return the count.
 *
 * Uses sched_getaffinity(2) so the result honours any cgroup/taskset the
 * operator applied. Returns 0 on failure (errno set).
 */
static int _allowed_cpus(int* out, int max)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    if(sched_getaffinity(0, sizeof set, &set) != 0)
    {
        return 0;
    }

    int n = 0;
    for(size_t cpu = 0u; cpu < (size_t)CPU_SETSIZE && n < max; ++cpu)
    {
        if(CPU_ISSET(cpu, &set))
        {
            out[n++] = (int)cpu;
        }
    }
    return n;
}

int srv_affinity_online_cpus(void)
{
    int cpus[CPU_SETSIZE];
    int n = _allowed_cpus(cpus, (int)(sizeof cpus / sizeof cpus[0]));
    if(n > 0)
    {
        return n;
    }

    long sc = sysconf(_SC_NPROCESSORS_ONLN);
    return (sc > 0) ? (int)sc : 1;
}

void srv_affinity_pin_self(const char* who, int slot)
{
    const char* tag = who ? who : "?";

    int cpus[CPU_SETSIZE];
    int n = _allowed_cpus(cpus, (int)(sizeof cpus / sizeof cpus[0]));
    if(n <= 0)
    {
        EML_WARN(LOG_TAG, "%s: cannot read CPU affinity mask (%s) — running unpinned", tag, strerror(errno));
        return;
    }

    if(slot < 0)
    {
        slot = 0;
    }
    int cpu = cpus[(unsigned)slot % (unsigned)n];

    cpu_set_t one;
    CPU_ZERO(&one);
    CPU_SET((size_t)cpu, &one);

    int rc = pthread_setaffinity_np(pthread_self(), sizeof one, &one);
    if(rc != 0)
    {
        /* Non-fatal by design: if the sandbox or platform refuses the pin the
         * server must still serve, just without the locality win. */
        EML_WARN(LOG_TAG, "%s(slot %d): pin to CPU %d failed (%s) — running unpinned", tag, slot, cpu, strerror(rc));
        return;
    }

    EML_INFO(LOG_TAG, "%s(slot %d) pinned to CPU %d (%d cores available)", tag, slot, cpu, n);
}
