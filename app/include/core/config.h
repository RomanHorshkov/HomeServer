/**
 * @file config.h
 * @brief Shared server status, size, time, and core configuration helpers.
 */

#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

/*****************************************************************************************************************************************
 * PUBLIC INCLUDES
 *****************************************************************************************************************************************
 */

#include <stddef.h>  /* size_t */
#include <stdint.h>  /* uint8_t, uint32_t */
#include <unistd.h>  // ssize_t

#include "emlog.h"   /* EML_ERROR, EML_PERR */

/*****************************************************************************************************************************************
 * PUBLIC ENUMERATED VARIABLES
 *****************************************************************************************************************************************
 */

typedef enum
{

    /**
     * @brief Generic failure status.
     */
    STATUS_FAILURE = -1,

    /**
     * @brief Generic success status.
     */
    STATUS_SUCCESS = 0

} status_t;

/*****************************************************************************************************************************************
 * PUBLIC DEFINES
 *****************************************************************************************************************************************
 */

/* size helpers */
#ifndef KiB
#    define KiB(x) ((size_t)(x) * 1024ULL)
#endif /* KiB */
#ifndef MiB
#    define MiB(x) (KiB(x) * 1024ULL)
#endif /* MiB */
#ifndef GiB
#    define GiB(x) (MiB(x) * 1024ULL)
#endif /* GiB */

/* time helpers */
#ifndef Seconds
#    define Seconds(x) ((size_t)(x))
#endif /* Seconds */
#ifndef Minutes
#    define Minutes(x) (Seconds(x) * 60ULL)
#endif /* Minutes */
#ifndef Hours
#    define Hours(x) (Minutes(x) * 60ULL)
#endif /* Hours */
#ifndef Days
#    define Days(x) (Hours(x) * 24ULL)
#endif /* Days */

/** CORE PROPERTIES */

/* Home page URI */
#define HOME_PAGE "index.html"

#endif /* SERVER_CONFIG_H */
