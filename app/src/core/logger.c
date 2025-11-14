/**
 * @file logger.c
 * @brief EMlog-backed logging bootstrap for the HomeServer daemon.
 *
 * This module configures the external EMlog library so every component in
 * the server emits consistent, structured log lines. Debug builds keep the
 * legacy stdout/stderr experience (handy for local hacking) while release
 * builds automatically forward everything to journald whenever the EMlog
 * static library was compiled with systemd support.
 *
 * The rest of the codebase continues to call the simple `log_info()` /
 * `log_error()` front-ends declared in `logger.h`; these are now thin
 * wrappers around EMlog macros so there is no runtime overhead per call.
 *
 * Behaviour summary:
 *   - DEBUG_MODE: emit at DEBUG level, timestamps enabled, stdout/stderr sink.
 *   - Release:    emit at INFO level, prefer journald sink, fall back to stdio.
 *
 * The journald identifier can be customised via `logger_init("name")`. When
 * not provided the identifier defaults to `"homeserver"`.
 */

#define _GNU_SOURCE

#include "logger.h"

#include <arpa/inet.h>  /* inet_ntop, AF_INET helpers                      */
#include <netdb.h>      /* struct addrinfo                                 */
#include <netinet/in.h> /* sockaddr_in / sockaddr_in6                      */
#include <stdbool.h>    /* bool                                            */
#include <stdio.h>      /* snprintf                                        */
#include <string.h>     /* strnlen, memcpy                                 */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

/** @brief Component name used for internal logger diagnostics. */
#define LOGGER_INTERNAL_TAG "logger"
/** @brief Maximum identifier length accepted by journald/systemd. */
#define LOGGER_IDENT_MAX 63

/****************************************************************************
 * PRIVATE STRUCTURES
 ****************************************************************************
 */

/**
 * @brief Process-wide logger state.
 *
 * Tracks whether EMlog has been initialised, the currently selected
 * journald identifier, and if the journald writer is active so we can
 * emit diagnostic breadcrumbs and tear down cleanly on shutdown.
 */
typedef struct
{
    bool initialized;          /**< Guard against duplicate init calls. */
    bool journald_active;      /**< True when emlog_enable_journald() succeeded. */
    char identifier[64];       /**< NUL-terminated journald identifier. */
} logger_state_t;

static logger_state_t g_logger = {0};

/****************************************************************************
 * PRIVATE FUNCTIONS
 ****************************************************************************
 */

/**
 * @brief Copy the chosen identifier into the global state.
 *
 * Journald truncates identifiers longer than 63 bytes; we mirror that limit
 * so the EMlog helper always receives a bounded string.
 *
 * @param ident Desired identifier (must be non-NULL).
 */
static void logger_store_identifier(const char *ident)
{
    size_t len = strnlen(ident, LOGGER_IDENT_MAX);
    memcpy(g_logger.identifier, ident, len);
    g_logger.identifier[len] = '\0';
}

/**
 * @brief Return either the provided identifier or the default one.
 *
 * @param ident Optional identifier string.
 * @return const char* Guaranteed non-empty identifier.
 */
static const char *logger_pick_identifier(const char *ident)
{
    return (ident && *ident) ? ident : LOGGER_IDENTIFIER_DEFAULT;
}

/****************************************************************************
 * PUBLIC FUNCTIONS
 ****************************************************************************
 */

void logger_init(const char *identifier)
{
    if(g_logger.initialized) return;

    const char *chosen = logger_pick_identifier(identifier);
    logger_store_identifier(chosen);

#ifdef DEBUG_MODE
    emlog_init(EML_LEVEL_DBG, true);
    emlog_set_level(EML_LEVEL_DBG);
    emlog_set_writev_flush(true);  /* keep stdout/stderr in sync with printf */
    g_logger.journald_active = false;
    EML_INFO(LOGGER_INTERNAL_TAG, "Debug logger active; stdout/stderr sink (id=%s)",
             g_logger.identifier);
#else
    emlog_init(EML_LEVEL_INFO, true);
    emlog_set_level(EML_LEVEL_INFO);

    if(emlog_has_journald() && emlog_enable_journald(g_logger.identifier))
    {
        g_logger.journald_active = true;
        EML_INFO(LOGGER_INTERNAL_TAG, "Journald sink enabled (identifier=%s)", g_logger.identifier);
    }
    else
    {
        g_logger.journald_active = false;
        emlog_set_writev_flush(true);
        EML_WARN(LOGGER_INTERNAL_TAG, "Journald sink unavailable, falling back to stdio");
    }
#endif

    g_logger.initialized = true;
}

void logger_close(void)
{
    if(!g_logger.initialized) return;

#ifndef DEBUG_MODE
    if(g_logger.journald_active)
    {
        EML_INFO(LOGGER_INTERNAL_TAG, "Disabling journald writer");
    }
#endif

    emlog_set_writer(NULL, NULL);
    g_logger.journald_active = false;
    g_logger.initialized     = false;
}

#ifdef DEBUG_MODE
void log_addrinfo_list(const struct addrinfo *ai)
{
    if(ai == NULL)
    {
        EML_INFO("net", "[logger]: addrinfo list is empty");
        return;
    }

    int  index = 0;
    char ip_str[INET6_ADDRSTRLEN];

    for(; ai != NULL; ai = ai->ai_next, ++index)
    {
        void       *addr   = NULL;
        const char *ipver  = "UNKNOWN";
        int         family = ai->ai_family;

        if(family == AF_INET)
        {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)ai->ai_addr;
            addr                     = &(ipv4->sin_addr);
            ipver                    = "IPv4";
        }
        else if(family == AF_INET6)
        {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)ai->ai_addr;
            addr                      = &(ipv6->sin6_addr);
            ipver                     = "IPv6";
        }
        else
        {
            EML_INFO("net", "[logger]: addrinfo[%d] Unknown family=%d", index, family);
            continue;
        }

        inet_ntop(family, addr, ip_str, sizeof ip_str);

        const char *protocol_name = "UNKNOWN";
        switch(ai->ai_protocol)
        {
            case IPPROTO_TCP:
                protocol_name = "TCP";
                break;
            case IPPROTO_UDP:
                protocol_name = "UDP";
                break;
            default:
                break;
        }

        EML_INFO("net",
                 "[logger]: addr_info[%d] %s address=%s socktype=%d protocol=%s flags=0x%x", index,
                 ipver, ip_str, ai->ai_socktype, protocol_name, ai->ai_flags);
    }
}
#endif /* DEBUG_MODE */
