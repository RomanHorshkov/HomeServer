#ifndef SERVER_SETTINGS_H
#define SERVER_SETTINGS_H

/****************************************************************************
 * PUBLIC ENUMERATED VARIABLES
 ****************************************************************************
 */

typedef enum
{
    STATUS_FAILURE = -1, /*< */
    STATUS_SUCCESS = 0,
} status_t;

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */

/* size helpers */
#define KiB(x)                    ((size_t)(x) * 1024ULL)
#define MiB(x)                    (KiB(x) * 1024ULL)
#define GiB(x)                    (MiB(x) * 1024ULL)

/** CORE PROPERTIES */

/* Home page URI */
#define HOME_PAGE "index.html"

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

#define HTTP_RECEIVE_BUFFER_LEN KiB(4)
#define HTTP_MAX_METHOD_LEN 8
#define HTTP_MAX_PATH_LEN KiB(1)
#define HTTP_MAX_HEADERS_IN 20
#define HTTP_MAX_HEADERS_OUT 12
#define HTTP_MAX_HEADER_NAME_LEN 64
#define HTTP_MAX_HEADER_VALUE_LEN KiB(1)
#define HTTP_MAX_BODY_RAM_CAPACITY KiB(16)

#endif /* SERVER_SETTINGS_H */
