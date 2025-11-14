/**
 * @file logger.h
 * @brief HomeServer logging facade (EMlog-backed).
 *
 * The HomeServer codebase keeps using the familiar `log_info()` /
 * `log_error()` helpers, but the implementation now delegates to the EMlog
 * library that ships alongside the external DataBase project. This header
 * exposes the lifecycle helpers (`logger_init`, `logger_close`) and maps
 * the legacy logging helpers to EMlog macros so every call site benefits
 * from structured output, thread safety, and journald integration.
 */

#ifndef SERVER_LOGGER_H
#define SERVER_LOGGER_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */
#include <errno.h>   /* errno, EADDRINUSE, etc. */
#include <string.h>  /* memset(), strcpy(), strlen(), strerror(), etc. */

#include <emlog.h>   /* External logging library */

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */

/** @brief Default component/journald identifier used by the logger. */
#define LOGGER_IDENTIFIER_DEFAULT "homeserver"
/** @brief Component label attached to log lines emitted via the macros below. */
#define LOGGER_COMPONENT_DEFAULT LOGGER_IDENTIFIER_DEFAULT

/****************************************************************************
 * PUBLIC FORWARD DECLARATIONS
 ****************************************************************************
 */
struct addrinfo;

/****************************************************************************
 * PUBLIC FUNCTIONS
 ****************************************************************************
 */

/**
 * @brief Initialize EMlog for this process.
 *
 * @param identifier Optional journald identifier/tag. Pass NULL to use
 *                   @ref LOGGER_IDENTIFIER_DEFAULT.
 */
void logger_init(const char *identifier);

/**
 * @brief Tear down the logger and restore default EMlog sinks.
 */
void logger_close(void);

/****************************************************************************
 * PUBLIC LOGGING MACROS
 ****************************************************************************
 */

/**
 * @brief Emit a DEBUG-level line (visible only when EMlog level <= DEBUG).
 */
#define log_debug(...) \
    emlog_log(EML_LEVEL_DBG, LOGGER_COMPONENT_DEFAULT, __VA_ARGS__)

/**
 * @brief Emit an INFO-level line to the configured EMlog sink.
 */
#define log_info(...) \
    emlog_log(EML_LEVEL_INFO, LOGGER_COMPONENT_DEFAULT, __VA_ARGS__)

/**
 * @brief Emit a WARN-level line to the configured EMlog sink.
 */
#define log_warn(...) \
    emlog_log(EML_LEVEL_WARN, LOGGER_COMPONENT_DEFAULT, __VA_ARGS__)

/**
 * @brief Emit an ERROR-level line to the configured EMlog sink.
 */
#define log_error(...) \
    emlog_log(EML_LEVEL_ERROR, LOGGER_COMPONENT_DEFAULT, __VA_ARGS__)

/**
 * @brief Convenience macro that records errno text at ERROR level.
 */
#define log_perror(...) \
    emlog_log_errno(EML_LEVEL_ERROR, LOGGER_COMPONENT_DEFAULT, errno, __VA_ARGS__)

#ifdef DEBUG_MODE
/**
 * @brief Log a list of socket addresses (debug helper).
 *
 * Iterates over a linked list of `addrinfo` results (e.g., from `getaddrinfo`)
 * and prints out address, protocol, socket type, and flags.
 *
 * @param ai Pointer to the first element of the `addrinfo` linked list.
 */
void log_addrinfo_list(const struct addrinfo *ai);
#endif /* DEBUG_MODE */

#endif /* SERVER_LOGGER_H */
