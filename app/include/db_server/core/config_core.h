/**
 * @file config_core.h
 * @brief Core listener, worker, timer, and fan-out configuration constants.
 */

#ifndef SERVER_CONFIG_CORE_H
#define SERVER_CONFIG_CORE_H

#include <db_server/core/config.h>

/*****************************************************************************************************************************************
 * PUBLIC ENUMERATED DECLARATIONS
 *****************************************************************************************************************************************
 */

typedef enum
{
    /**
     * @brief Uninitialized: not yet started.
     */
    SERVER_STATUS_UNINITIALIZED = 0,
    /**
     * @brief Active: ready to receive new clients.
     */
    SERVER_STATUS_ACTIVE        = 1,
    /**
     * @brief Shutdown: shutting down and cleaning up.
     */
    SERVER_STATUS_SHUTDOWN      = 2,
    /**
     * @brief Invalid: max value for server status.
     */
    SERVER_STATUS_INVALID       = 3,
} server_status;

typedef enum
{
    LISTENER_STATUS_INACTIVE = 0, /* listener is inactive */
    LISTENER_STATUS_ACTIVE   = 1, /* listener is active */
    LISTENER_STATUS_PAUSED   = 2, /* listener is paused */
    LISTENER_STATUS_SHUTDOWN = 3, /* listener to shutdown */
    LISTENER_STATUS_INVALID  = 4, /* max value for listener status */
} listener_status_t;

/**
 * @brief Max Epoll Batch Size: The maximum number of events to retrieve
 * and process in a single epoll_wait() call.
 *
 * This is NOT a limit on the number of connected clients. A larger batch size reduces syscall overhead under high load.
 */
#define MAX_FAN_OUT_SOCKETS                          64U

/*****************************************************************************************************************************************
 * LISTENER PROPERTIES
 *****************************************************************************************************************************************
 */
/* Max listening sockets: API (ipv4 + ipv6) + the dedicated upload listener (ipv4 + ipv6) */
#define SERVER_CORE_MAX_LISTENING_SOCKETS            4U

/* Max pending connections on one listener socket */
#define SERVER_CORE_MAX_PENDING_SOCKETS_PER_LISTENER 8U

/*****************************************************************************************************************************************
 * WORKER PROPERTIES
 *****************************************************************************************************************************************
 */

/* Max clients amount */
#define WORKER_MAX_CLIENTS                           64U

/* Client short timeout [s]: applied before first activity (initial request) */
#define WORKER_CLIENT_TIMEOUT_SHORT                  Seconds(15U)

/* Client long timeout [s]: applied after the first successful activity */
#define WORKER_CLIENT_TIMEOUT_LONG                   Minutes(1U)

/* Operator timer tick while clients are present [s] */
#define OPERATOR_TIMER_PERIOD_SHORT                  Seconds(5U)

/* Operator timer tick when idle [s] */
#define OPERATOR_TIMER_PERIOD_LONG                   Minutes(5U)

/*****************************************************************************************************************************************
 * UPLOAD WORKER POOL PROPERTIES  (socket_rearchitecturing.md — upload isolation)
 *****************************************************************************************************************************************
 */

/* Upload worker threads. Uploads run on their OWN pool (never inside an API operator), so a slow upload can never
 * head-of-line-block API traffic. Embedded style: a fixed, build-time count sized to the box. This is also the
 * per-server upload concurrency: a (WORKER_UPLOAD_COUNT + WORKER_UPLOAD_QUEUE_DEPTH)-th concurrent upload gets 503.
 * Each worker consumes one DB_app transaction slot ABOVE the operators' [0, ops) range. */
#define WORKER_UPLOAD_COUNT                          4U

/* Absolute wall-clock ceiling for one upload, CLOCK_MONOTONIC (defeats a forever-slow-but-never-idle client). */
#define WORKER_UPLOAD_MAX_WALL_S                     7200U /* 2 h — a 4 GiB upload at ~600 KiB/s still fits */

#endif /* SERVER_CONFIG_CORE_H */
