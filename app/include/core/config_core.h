#ifndef SERVER_CONFIG_CORE_H
#define SERVER_CONFIG_CORE_H


#include "config.h"


/****************************************************************************
 * PUBLIC ENUMERATED DECLARATIONS
 ****************************************************************************
 */

typedef enum
{
    WORKER_STATUS_INACTIVE = 0, /* worker is inactive */
    WORKER_STATUS_ACTIVE = 1,   /* worker is active */
    WORKER_STATUS_FULL = 2,     /* worker is full */
    WORKER_STATUS_SHUTDOWN = 3, /* worker to shutdown */
    WORKER_STATUS_INVALID = 4,  /* max value for worker status */
} worker_status_t;

typedef enum
{
    SERVER_STATUS_INACTIVE = 0, /* server is inactive */
    SERVER_STATUS_ACTIVE = 1,   /* server is active */
    SERVER_STATUS_SHUTDOWN = 2, /* server is shutting down */
    SERVER_STATUS_INVALID = 3,  /* max value for server status */
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
 * @brief Fan-Out: the number of independent endpoints that can become
 * ready at the same time—e.g. how many client sockets might have data
 * waiting when call epoll_wait().
 * one slot per possible registration */
#define MAX_FAN_OUT_SOCKETS 8U

/****************************************************************************
 * CORE PROPERTIES
 ****************************************************************************
 */
/* Server timeout */
#define SERVER_KEEPALIVE_TIMEOUT_ALONE 60U
#define SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE 10U

/****************************************************************************
 * PIPELINE PROPERTIES
 ****************************************************************************
 */

/* Capacity of the SPSC ring buffer for file descriptors */
#define PIPELINE_SPSC_RING_CAPACITY 8U

/* Total sockets needed for the pipeline */
#define PIPELINE_MAX_SOCKETS 3  // wakeup_fd and pipe

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

/* Client short timeout [s] */
#define WORKER_CLIENT_TIMEOUT_SHORT 5

/* Client long timeout [s] */
#define WORKER_CLIENT_TIMEOUT_LONG 30





#endif /* SERVER_CONFIG_CORE_H */
