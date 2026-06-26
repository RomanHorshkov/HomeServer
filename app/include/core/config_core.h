#ifndef SERVER_CONFIG_CORE_H
#define SERVER_CONFIG_CORE_H


#include "config.h"


/****************************************************************************
 * PUBLIC ENUMERATED DECLARATIONS
 ****************************************************************************
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
    SERVER_STATUS_ACTIVE = 1,
    /**
     * @brief Shutdown: shutting down and cleaning up.
     */
    SERVER_STATUS_SHUTDOWN = 2,
    /**
     * @brief Invalid: max value for server status.
     */
    SERVER_STATUS_INVALID = 3,
} server_status;

typedef enum
{
    LISTENER_STATUS_INACTIVE = 0, /* listener is inactive */
    LISTENER_STATUS_ACTIVE = 1,   /* listener is active */
    LISTENER_STATUS_PAUSED = 2,   /* listener is paused */
    LISTENER_STATUS_SHUTDOWN = 3, /* listener to shutdown */
    LISTENER_STATUS_INVALID = 4,  /* max value for listener status */
} listener_status_t;


/**
 * @brief Max Epoll Batch Size: The maximum number of events to retrieve
 * and process in a single epoll_wait() call.
 * 
 * This is NOT a limit on the number of connected clients.
 * A larger batch size reduces syscall overhead under high load.
 */
#define MAX_FAN_OUT_SOCKETS 64U

/****************************************************************************
 * LISTENER PROPERTIES
 ****************************************************************************
 */
/* Max listening sockets amount, 1 ipv4 + 1 ipv6 */
#define SERVER_CORE_MAX_LISTENING_SOCKETS 2U

/* Max pending connections on one listener socket */
#define SERVER_CORE_MAX_PENDING_SOCKETS_PER_LISTENER 8U


/****************************************************************************
 * WORKER PROPERTIES
 ****************************************************************************
 */

/* Max clients amount */
#define WORKER_MAX_CLIENTS 64U

/* Client short timeout [s]: applied before first activity (initial request) */
#define WORKER_CLIENT_TIMEOUT_SHORT Seconds(15U)

/* Client long timeout [s]: applied after the first successful activity */
#define WORKER_CLIENT_TIMEOUT_LONG Minutes(1U)

/* Operator timer tick while clients are present [s] */
#define OPERATOR_TIMER_PERIOD_SHORT Seconds(5U)

/* Operator timer tick when idle [s] */
#define OPERATOR_TIMER_PERIOD_LONG Minutes(5U)


#endif /* SERVER_CONFIG_CORE_H */
