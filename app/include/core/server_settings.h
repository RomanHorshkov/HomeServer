#ifndef SERVER_SETTINGS_H
#define SERVER_SETTINGS_H

/****************************************************************************
 * PUBLIC ENUMERATED VARIABLES
 ****************************************************************************
 */

enum status
{
    STATUS_FAILURE = -1,               /* error occurred */
    STATUS_SUCCESS = 0,                /* everything is fine */
    CLIENT_NEW_CLIENT_CREATED,         // new client created
    CLIENT_NEW_SOCKET_CREATED,         // new socket created for an existing client
    CLIENT_MANAGER_NEW_CLIENT_NONE,    // no new client
    CLIENT_MANAGER_CLIENT_NOT_EXISTS,  // new client process
    CLIENT_MANAGER_CLIENT_EXISTS,      // new socket for an existing client
};

enum server_status
{
    SERVER_STATUS_INACTIVE = 0, /* server is inactive */
    SERVER_STATUS_ACTIVE = 1,   /* server is active */
    SERVER_STATUS_SHUTDOWN = 2, /* server is shutting down */
};

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
#define MAX_CLIENTS 100

// /* Max sockets per client */
// #define MAX_SOCKETS_PER_CLIENT 10

/* Max pending connections on one listener socket */
#define MAX_PENDING_CONNECTIONS 10

//
#define MAX_EVENTS 64

/* Sleep time */
#define SERVER_SLEEP_TIME_S 0
#define SERVER_SLEEP_TIME_MS 50
#define SERVER_SLEEP_TIME_US ((SERVER_SLEEP_TIME_MS) * 1000)
#define SERVER_SLEEP_TIME_NS ((SERVER_SLEEP_TIME_US) * 1000)

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
#define HTTP_SEND_BUFFER_LEN 8192

#define HTTP_MAX_METHOD_LEN 8
#define HTTP_MAX_PATH_LEN 1024
#define HTTP_MAX_HEADER_COUNT 20
#define HTTP_MAX_HEADER_NAME_LEN 64
#define HTTP_MAX_HEADER_VALUE_LEN 256

/*
 * WEBSITE SETTINGS
 */
/* Home page URI */
#ifdef DEBUG_MODE
#    define WEBSITE_HOME_PAGE "views/index.html"
// #else
// #define WEBSITE_HOME_PAGE "views/index.html"
#endif /* DEBUG_MODE */

#endif /* SERVER_SETTINGS_H */
