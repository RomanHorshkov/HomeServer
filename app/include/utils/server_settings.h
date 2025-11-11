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
    WORKER_STATUS_SHUTDOWN = 3, /* worker to shutdown */
    WORKER_STATUS_INVALID = 4,  /* max value for worker status */
} worker_status;

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */

/** PIPELINE PROPERTIES */
/* Capacity of the SPSC ring buffer for file descriptors */
#define SPSC_RING_CAPACITY 8U

/* Total sockets needed for the pipeline */
#define PIPELINE_MAX_SOCKETS 3  // wakeup_fd and pipe

/** LISTENER PROPERTIES */
/* Max listening sockets amount */
#define MAX_LISTENERS 2U

/* Max pending connections on one listener socket */
#define MAX_PENDING_CONNECTIONS 8U

/** WORKER PROPERTIES */
/* Max clients amount */
#define MAX_CLIENTS 64U

/* Client short timeout [s] */
#define CLIENT_TIMEOUT_S 5

/* Client long timeout [s] */
#define CLIENT_TIMEOUT_L 120

/* First available socket for clients */
#define CLIENT_FIRST_SOCKET 12

/* Fan-Out: the number of independent endpoints that can become ready at the same time—e.g. how many
 * client sockets might have data waiting when you call epoll_wait().*/
#define MAX_FAN_OUT_SOCKETS 8U

/** CORE PROPERTIES */

/* Home page URI */
#define HOME_PAGE "index.html"

/* chdir and chroot paths */
#define CHDIR_PATH "/srv/home_server/pri"  // DO NOT WRITE /var/www as global path

/* Server timeout */
#define SERVER_KEEPALIVE_TIMEOUT_ALONE 60U
#define SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE 10U

/*
 * HTTP PROPERTIES
 */

/**
 * @brief Supported HTTP methods.
 */
typedef enum
{
    HTTP_METHOD_GET,    /* GET method */
    HTTP_METHOD_PUT,    /* PUT method */
    HTTP_METHOD_POST,   /* POST method */
    HTTP_METHOD_DELETE, /* DELETE method */
    HTTP_METHOD_UNKNOWN /* Unknown method */
} http_method_t;

#define HTTP_RECEIVE_BUFFER_LEN 4096
#define HTTP_MAX_METHOD_LEN 8
#define HTTP_MAX_PATH_LEN 1024
#define HTTP_MAX_HEADERS_IN 20
#define HTTP_MAX_HEADERS_OUT 12
#define HTTP_MAX_HEADER_NAME_LEN 64
#define HTTP_MAX_HEADER_VALUE_LEN 256
#define HTTP_MAX_BODY_RAM_CAPACITY (64 * 1024) /* 64 KB */

#endif /* SERVER_SETTINGS_H */
