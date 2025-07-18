#ifndef SERVER_SETTINGS_H
#define SERVER_SETTINGS_H

/****************************************************************************
 * PUBLIC ENUMERATED VARIABLES
 ****************************************************************************
 */

enum status
{
    STATUS_FAILURE = -1, /* error occurred */
    STATUS_SUCCESS = 0,  /* everything is fine */
};

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
} listener_status;

typedef enum
{
    WORKER_STATUS_INACTIVE = 0, /* worker is inactive */
    WORKER_STATUS_ACTIVE = 1,   /* worker is active */
    WORKER_STATUS_FULL = 2,     /* worker is full */
    WORKER_STATUS_ERROR = 3,    /* worker error */
    WORKER_STATUS_SHUTDOWN = 4, /* worker to shutdown */
    WORKER_STATUS_INVALID = 5,  /* max value for worker status */
} worker_status;

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */

/*
 * SERVER PROPERTIES
 */

/* Max listeners amount */
#define MAX_LISTENERS 2

/* Max clients amount */
#define MAX_CLIENTS 64

/* Capacity of the SPSC ring buffer for file descriptors */
#define SPSC_RING_CAPACITY 8

/* Fan-Out: the number of independent endpoints that can become ready at the same time—e.g. how many
 * client sockets might have data waiting when you call epoll_wait().*/
#define MAX_FAN_OUT_SOCKETS 8

/* Max pending connections on one listener socket */
#define MAX_PENDING_CONNECTIONS 8

/* Time measurement unit */
#define _S 1UL
#define _MS ((_S) * 1000)
#define _US ((_MS) * 1000)
#define _NS ((_US) * 1000)

/* Server timeout */
#define SERVER_KEEPALIVE_TIMEOUT_ALONE (60UL * _S)
#define SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE (10UL * _S)

/*
 * SOCKET PROPERTIES
 */

/* Client short timeout [s] */
#define CLIENT_MAX_TIMEOUT_S 5

/* Client long timeout [s] */
#define CLIENT_MAX_TIMEOUT_S_L 120

/* Client short timeout [ms] */
#define CLIENT_MAX_TIMEOUT_MS 0

/*
 * HTTP PROPERTIES
 */

#define HTTP_RECEIVE_BUFFER_LEN 4096
#define HTTP_MAX_METHOD_LEN 8
#define HTTP_MAX_PATH_LEN 1024
#define HTTP_MAX_HEADER_COUNT 20
#define HTTP_MAX_HEADER_NAME_LEN 64
#define HTTP_MAX_HEADER_VALUE_LEN 256
#define HTTP_MAX_BODY_RAM_CAPACITY (64 * 1024) /* 64 KB */

/*
 * WEBSITE SETTINGS
 */
/* Home page URI */
// #ifdef DEBUG_MODE
#define WEBSITE_HOME_PAGE "views/index.html"
// #else
// #define WEBSITE_HOME_PAGE "views/index.html"
// #endif /* DEBUG_MODE */

#endif /* SERVER_SETTINGS_H */
