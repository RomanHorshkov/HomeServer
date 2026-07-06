/**
 * @file affinity.h
 *
 * @brief CPU affinity pinning for the server threads — one core per thread.
 *
 * On a dedicated box the platform should use every core deeply. The server
 * already reserves one CPU for the listener/dispatcher and gives the rest to
 * operator threads (§ worker sizing); pinning each thread to its own core
 * removes cross-core migration and cache-line bouncing, which measurably
 * improves tail latency under load.
 *
 * "Online" here means the process's *allowed* CPU set (scheduler affinity
 * mask), read via sched_getaffinity(2) — NOT the machine-wide count — so it
 * automatically respects any cgroup `AllowedCPUs=` / `taskset` the operator
 * imposes. Pinning uses sched_setaffinity(2); the systemd unit's syscall
 * filter permits exactly that call (see install/systemd/home_server.service).
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    jul 2026
 * (c) 2026
 */
#ifndef DB_SERVER_UTILS_AFFINITY_H
#define DB_SERVER_UTILS_AFFINITY_H

/**
 * @brief Number of CPUs the process is allowed to run on (its affinity mask).
 *        Falls back to sysconf(_SC_NPROCESSORS_ONLN), and never returns < 1.
 */
int srv_affinity_online_cpus(void);

/**
 * @brief Pin the CALLING thread to a single CPU: allowed_cpus[slot % online].
 *
 * Never fatal: on any failure it logs a warning and leaves the thread free to
 * float (the scheduler still runs it). Convention: the listener uses slot 0,
 * operator i uses slot i+1, so with C cores the listener owns CPU0 and each
 * operator owns its own CPU; when operators outnumber cores the mapping wraps.
 *
 * @param who   short label for logs ("listener", "operator", ...)
 * @param slot  logical slot (>= 0); values are taken modulo the online count
 */
void srv_affinity_pin_self(const char* who, int slot);

#endif /* DB_SERVER_UTILS_AFFINITY_H */
