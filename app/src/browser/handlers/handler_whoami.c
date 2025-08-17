/**
 * handler_whoami.c
 * ----------------
 * Implements the /api/whoami endpoint handler for the web server.
 *
 * Responds with a JSON object containing:
 *   - The current server time (ISO-8601 with milliseconds)
 *   - The HTTP method and path
 *   - All request headers echoed back
 *
 * This handler receives a parsed HttpRequest and fills a fresh HttpResponse.
 * No socket I/O is performed here; the caller is responsible for network
 * transmission and memory cleanup.
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑05‑11
 * (c) 2025
 */
#define _GNU_SOURCE

#include "handler_whoami.h"

#include <sys/time.h> /* gettimeofday() */
#include <time.h>     /* gmtime_r(), strftime() */

#include "cJSON.h"
#include "contract_version.h" /* CONTRACT_VERSION */
#include "handler_utils.h"
#include "route_register.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int handler_whoami(const HttpRequest *req, HttpResponse *res)
{
    /*-----------------------------------------------------------------------
     * (1)  Compute an ISO‑8601 timestamp with milliseconds.                 *
     *      Use gettimeofday() for µs resolution, convert to UTC, then       *
     *      format as “YYYY‑MM‑DDThh:mm:ss.mmmZ“.                            *
     *---------------------------------------------------------------------*/

    static struct tm tm;      /* broken‑down UTC    */
    static char timestr[32];  /*  “YYYY-MM-DDThh:mm:ss” */
    static struct timeval tv; /* wall‑clock with µs */

    gmtime_r(&tv.tv_sec, &tm); /* thread‑safe gmtime */
    gettimeofday(&tv, NULL);   /* POSIX; never fails */
    strftime(timestr, sizeof timestr, "%Y-%m-%dT%H:%M:%S", &tm);

    char iso_time[32]; /* final “…mmmZ” string */

    int len =
        snprintf(iso_time, sizeof iso_time, "%s.%03ldZ", timestr, tv.tv_usec / 1000L); /* µs → ms */

    /* Check for buffer overrun */
    if(len < 0 || len >= (int)sizeof iso_time)
    {
        /* Fallback – keep only the seconds part (better than truncation)   */
        strncpy(iso_time, timestr, sizeof iso_time - 1);
        iso_time[sizeof iso_time - 1] = '\0';
    }

    /*-----------------------------------------------------------------------
     * (2)  Build the JSON structure using cJSON.                           *
     *---------------------------------------------------------------------*/
    cJSON *root = cJSON_CreateObject();

    /* "path":   "/api/whoami" */
    cJSON_AddStringToObject(root, "path", req->path);

    /* API contrct version */
    cJSON_AddStringToObject(root, "contract_version", CONTRACT_VERSION);

    /* server time */
    cJSON_AddStringToObject(root, "server_time", iso_time);

    /* "method": "GET" */
    cJSON_AddStringToObject(root, "method", http_method_to_string(req->method));

    /* nested object */
    cJSON *hdrs = cJSON_AddObjectToObject(root, "headers");

    /* copy every header */
    for(int i = 0; i < req->header_count; ++i)
    {
        cJSON_AddStringToObject(hdrs, req->header_names[i], /* key   */
                                req->header_values[i]);     /* value */
    }

    if(validate_whoami_shape(root) != 0)
    {
        log_error("[whoami] schema shape check failed");
        cJSON_Delete(root);
        send_500(res);
        return -1;
    }

    char *body = cJSON_PrintUnformatted(root); /* compact JSON string */
    cJSON_Delete(root);                        /* free DOM tree       */

    /*-----------------------------------------------------------------------
     * (3)  Populate the HttpResponse structure for the caller.             *
     *---------------------------------------------------------------------*/
    res->status_code = 200;                 /* HTTP 200 OK              */
    res->status_text = "OK";                /* reason phrase            */
    res->content_type = "application/json"; /* MIME type                */
    res->body = body;                       /* transfer ownership       */
    res->body_length = strlen(body);        /* cache len for sender     */

    /* Optional: add a response header your HTTP layer will serialize */
    // http_response_add_header(res, "X-Contract-Version", CONTRACT_VERSION);

    return 0; /* success                  */
}

REGISTER_ROUTE("/api/whoami", handler_whoami)
