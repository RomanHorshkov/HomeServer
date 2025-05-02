#ifndef SERVER_SETTINGS_H
#define SERVER_SETTINGS_H


/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */

 
/****************************************************************************
 * SERVER PROPERTIES
 ****************************************************************************
 */

/* Max listeners amount */
#define MAX_LISTENERS 2

/* Max clients amount */
#define MAX_CLIENTS 10

/* Max pending connections amount */
#define MAX_PENDING_CONNECTIONS 10

/* Sleep time */
#define SERVER_SLEEP_TIME_S  0
#define SERVER_SLEEP_TIME_MS 50
#define SERVER_SLEEP_TIME_US ((SERVER_SLEEP_TIME_MS) * 1000)
#define SERVER_SLEEP_TIME_NS ((SERVER_SLEEP_TIME_US) * 1000)


/****************************************************************************
 * SOCKET PROPERTIES
 ****************************************************************************
 */

/* Client short timeout [s] */
#define CLIENT_MAX_TIMEOUT_S 30

/* Client long timeout [s] */
#define CLIENT_MAX_TIMEOUT_S_L 120

/* Client short timeout [ms] */
#define CLIENT_MAX_TIMEOUT_MS 0


/****************************************************************************
 * HTTP PROPERTIES
 ****************************************************************************
 */

#define HTTP_RECEIVE_BUFFER_LEN 4096
#define HTTP_SEND_BUFFER_LEN 8192
 
#define HTTP_MAX_METHOD_LEN 8
#define HTTP_MAX_PATH_LEN 1024
#define HTTP_MAX_HEADER_COUNT 20
#define HTTP_MAX_HEADER_NAME_LEN 64
#define HTTP_MAX_HEADER_VALUE_LEN 256


#endif /* SERVER_SETTINGS_H */