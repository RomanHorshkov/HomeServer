#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include "logger.h"

#include <arpa/inet.h>  // inet_ntop(), inet_pton()
#include <netdb.h>      // getaddrinfo(), addrinfo, gai_strerror()
#include <stdarg.h>     // varg
#include <stdio.h>      // FILE
#include <stdlib.h>     // exit(), malloc(), free()
#include <time.h>       // For timestamps

/****************************************************************************
 * PRIVATE VARIABLES DEFINITIONS
 ****************************************************************************/
static FILE *log_file = NULL;

/****************************************************************************
 * PRIVATE FUNCTIONS DECLARATIONS
 ****************************************************************************/

static void log_timestamp();
static void log_internal(const char *level, const char *fmt, va_list args);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************/

void logger_init(const char *filename)
{
    /* check if file not yet opened */
    if(log_file == NULL)
    {
        /* "w" = overwrite on server restart */
        log_file = fopen(filename, "w");
        if(!log_file)
        {
            perror("Failed to open log file");
            log_file = stdout;  // fallback
        }
    }
}

void logger_close()
{
    if(log_file && log_file != stdout)
    {
        fclose(log_file);
    }
}

void log_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_internal("INFO", fmt, args);
    va_end(args);
}

void log_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_internal("ERROR", fmt, args);
    va_end(args);
}

void log_addrinfo_list(const struct addrinfo *ai)
{
    int index = 0;
    char ip_str[INET6_ADDRSTRLEN];

    for(; ai != NULL; ai = ai->ai_next, ++index)
    {
        void *addr;
        const char *ipver;

        if(ai->ai_family == AF_INET)
        {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)ai->ai_addr;
            addr = &(ipv4->sin_addr);
            ipver = "IPv4";
        }
        else if(ai->ai_family == AF_INET6)
        {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)ai->ai_addr;
            addr = &(ipv6->sin6_addr);
            ipver = "IPv6";
        }
        else
        {
            log_info("[%d] Unknown address family: %d\n", index, ai->ai_family);
            continue;
        }

        /* Convert the IP to a string */
        inet_ntop(ai->ai_family, addr, ip_str, sizeof ip_str);

        /* Resolve protocol name */
        char protocol_name[NI_MAXSERV];

        if(ai->ai_protocol == IPPROTO_TCP)
        {
            strcpy(protocol_name, "TCP");
        }
        else if(ai->ai_protocol == IPPROTO_UDP)
        {
            strcpy(protocol_name, "UDP");
        }
        else
        {
            strcpy(protocol_name, "UNKNOWN");
        }

        log_info("[%d] %s address: %s | socktype: %d | protocol: %s | flags: 0x%x\n", index, ipver,
                 ip_str, ai->ai_socktype, protocol_name, ai->ai_flags);
    }
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************/

static void log_timestamp()
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", tm_info);
    fprintf(log_file, "%s ", buffer);
}

static void log_internal(const char *level, const char *fmt, va_list args)
{
    if(!log_file) return;

    log_timestamp();
    fprintf(log_file, "[%s] ", level);
    vfprintf(log_file, fmt, args);
    fprintf(log_file, "\n");
    fflush(log_file);  // flush every line for safety
}
