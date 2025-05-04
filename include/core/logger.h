#ifndef SERVER_LOGGER_H
#define SERVER_LOGGER_H

/**
 * @file logger.h
 * @brief Logging interface for the server.
 *
 * Provides simple timestamped logging with support for info/error levels
 * and socket address logging. Logs are written to a file (default: `server.log`)
 * and flushed on each write.
 */

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */
#include <errno.h>   // errno, EADDRINUSE, etc.
#include <string.h>  // memset(), strcpy(), strlen(), strerror(), etc.

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
 */

struct addrinfo;

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

 /**
 * @brief Initialize the logger.
 *
 * Opens the given file for writing (overwriting any existing content).
 * If the file cannot be opened, logging will fallback to `stdout`.
 *
 * @param filename The name of the file to log to (e.g., "server.log")
 */
void logger_init(const char *filename);

/**
 * @brief Close the logger.
 *
 * Closes the log file if it's not stdout. Safe to call even if the logger
 * was not successfully initialized.
 */
void logger_close();

/**
 * @brief Log an informational message.
 *
 * Accepts a `printf`-style format string with optional variadic arguments.
 * The message is timestamped and prefixed with `[INFO]`.
 *
 * @param fmt Format string (like `printf`)
 * @param ... Optional arguments
 */
void log_info(const char *fmt, ...);

/**
 * @brief Log an error message.
 *
 * Accepts a `printf`-style format string with optional variadic arguments.
 * The message is timestamped and prefixed with `[ERROR]`.
 *
 * @param fmt Format string (like `printf`)
 * @param ... Optional arguments
 */
void log_error(const char *fmt, ...);

/**
 * @brief Log a list of socket addresses.
 *
 * Iterates over a linked list of `addrinfo` results (e.g., from `getaddrinfo`)
 * and prints out address, protocol, socket type, and flags.
 *
 * @param ai Pointer to the first element of the `addrinfo` linked list
 */
void log_addrinfo_list(const struct addrinfo *ai);

#endif /* SERVER_LOGGER_H */