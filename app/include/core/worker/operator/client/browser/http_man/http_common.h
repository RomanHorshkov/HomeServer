/**
 * @file http_common.h
 * 
 * @brief Common HTTP definitions and helpers.
 * 
 * This file serves as a template for creating new header files in the project.
 * It includes standard sections for includes, defines, enumerated types,
 * structured types, variables, and function declarations.
 * 
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    dec 2025
 * (c) 2025
 */
#ifndef PROJECT_MODULE_FILE_H
#define PROJECT_MODULE_FILE_H

/****************************************************************************
 * INCLUDES
 ****************************************************************************
 */
#include "config_core.h"
#include "string_view.h"

/****************************************************************************
 * DEFINES
 ****************************************************************************
 */

/*
 * HTTP PROPERTIES
 */

/**
 * @brief Maximum length of the HTTP receive buffer.
 */
#define HTTP_RECEIVE_BUFFER_LEN KiB(32)

/**
 * @brief Maximum lengths and counts for HTTP parsing.
 */
#define HTTP_MAX_METHOD_LEN             8
#define HTTP_MAX_PATH_LEN               KiB(1)

/**
 * @brief Maximum number of headers allowed per request.
 */
#define HTTP_MAX_HEADERS_IN             24U

/**
 * @brief Maximum number of headers allowed per response.
 */
#define HTTP_MAX_HEADERS_OUT            12U

/**
 * @brief Maximum HTTP header name length.
 */
#define HTTP_MAX_HEADER_NAME_LEN        128U

/**
 * @brief Maximum HTTP header value length.
 */
#define HTTP_MAX_HEADER_VALUE_LEN       KiB(4)

/**
 * @brief Maximum HTTP body size to buffer in RAM.
 */
#define HTTP_MAX_BODY_RAM_CAPACITY      KiB(16)

/****************************************************************************
 * ENUMERATED TYPEDEFS
 ****************************************************************************
 */

/**
 * @brief Connection policy for HTTP/1.x.
 */
typedef enum
{
    /**
     * @brief Keep connection alive.
     */
    HTTP_CONNECTION_KEEP_ALIVE = 0,

    /**
     * @brief Close connection after response.
     */
    HTTP_CONNECTION_CLOSE = 1,
} Http_connection_policy_t;

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

/**
 * @brief Parsed HTTP request.
 */
typedef struct 
{
    uint8_t  thread_id;                     /* operator thread id carrying the request */
    uint64_t timestamp;                     /* request timestamp (epoch) */
    uint32_t remote_ip_be;                  /* peer IPv4 (network order), optional */
    uint16_t remote_port_be;                /* peer port (network order), optional */
    uint8_t header_count;                   /* Number of headers parsed */
    http_method_t method;                   /* HTTP method (GET, POST, etc.) */
    sv_t path;                              /* Request path */
    sv_t header_names[HTTP_MAX_HEADERS_IN]; /* Header names */
    sv_t header_values[HTTP_MAX_HEADERS_IN];/* Header values */
    sv_t body;                              /* Request body buffer */

    uint8_t connection_policy;              /* Connection policy (keep-alive or close) */
    uint8_t message_complete;               /* set when llhttp signals completion */

} http_request_t;

/****************************************************************************
 * ENUMERATED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * STRUCTURED TYPEDEFS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * STRUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */
/* None */

#endif /* PROJECT_MODULE_FILE_H */
