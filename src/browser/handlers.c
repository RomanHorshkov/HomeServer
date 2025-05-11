/*
 * handlers.c
 * ----------
 * Endpoint handlers for the tiny web‑server:
 *   - /api/whoami    -> JSON with request echo & server time
 *   - /api/drive     -> JSON directory listing for the virtual “Drive”
 *
 * The file also provides a minimal percent‑decoder for URL paths.
 *
 * (c) 2025 Roman Horshkov
 */

#define _GNU_SOURCE

/* Public interface */
#include "handlers.h"

/* libc / POSIX */
#include <ctype.h>    /* isxdigit()                                           */
#include <dirent.h>   /* opendir(), readdir(), closedir()                     */
#include <errno.h>    /* errno                                                */
#include <limits.h>   /* PATH_MAX, NAME_MAX                                   */
#include <stdio.h>    /* snprintf(), sscanf()                                 */
#include <stdlib.h>   /* malloc(), free(), strdup(), strtol()                 */
#include <string.h>   /* strcmp(), strcpy(), strerror(), strstr(), strdup()   */
#include <sys/stat.h> /* stat(), struct stat                                  */
#include <sys/time.h> /* gettimeofday()                                       */
#include <time.h>     /* gmtime_r(), strftime()                               */

/* third‑party / project */
#include "cJSON.h"
#include "logger.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

/* stringify PATH_MAX to build “%<PATH_MAX>s” format strings with sscanf()   */
#define _STR(x) #x
#define _XSTR(x) _STR(x)
#define PATH_MAX_STR _XSTR(PATH_MAX)

/* Allow room for “www” prefix + path + NUL                                   */
#define FULLPATH_MAX (PATH_MAX + 4)
/* Allow room for fullpath + “/” + NAME_MAX + NUL                             */
#define CHILDFULL_MAX (FULLPATH_MAX + NAME_MAX + 1)

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * url_decode()
 * ------------
 * Percent‑decodes @p src and writes the result to @p dst.
 *
 *  • UTF‑8 is *not* interpreted: the function operates on raw bytes.
 *  • The caller must supply a destination buffer of at least @p dst_sz bytes.
 *  • The output is always NUL‑terminated on success.
 *
 * Decoding rules (matching classic URL encoding):
 *   %xx   → single byte with hexadecimal value xx
 *   '+'   → ASCII space ' '
 *   other → byte copied unchanged
 *
 * @param src     NUL‑terminated percent‑encoded input string.
 * @param dst     Output buffer (may alias neither @p src nor overlap).
 * @param dst_sz  Size of @p dst in bytes, including the final NUL.
 *
 * @retval  0 Success – the string was fully decoded and fits in @p dst.
 * @retval -1 Failure – the decoded string would overflow @p dst.
 */
static int url_decode(const char *src, char *dst, size_t dst_sz);

/**
 * @brief Populate an #HttpResponse with a **400 Bad Request** JSON error.
 *
 * This helper is meant to be called by request handlers when they detect an
 * invalid client input. It allocates a tiny, compact JSON body that embeds
 * the supplied message and fills every mandatory field of the response so
 * that the caller can immediately stream it back to the socket.
 *
 * The generated payload has the form:
 * @code{.json}
 * {"error":"<msg>"}
 * @endcode
 *
 * ### Field assignments
 * | Member            | Value set by this routine                |
 * |-------------------|-------------------------------------------|
 * | `status_code`     | `400`                                    |
 * | `status_text`     | `"Bad Request"`                          |
 * | `content_type`    | `"application/json"`                     |
 * | `body`            | Heap‑allocated buffer containing JSON    |
 * | `body_length`     | `strlen(body)`                           |
 *
 * Allocation is performed with `malloc(3 + strlen(msg) + 12)` – if it fails
 * the function returns **‑1** and leaves @p resp untouched so the caller can
 * fall back to a 500‑level reply.
 *
 * @param[out] resp  Pointer to an uninitialised #HttpResponse structure.
 * @param      msg   Explanatory text to embed inside the JSON payload.
 *
 * @retval  0 Success – @p resp is fully initialised; its `body` must later be
 *            released with `free()`.
 * @retval -1 Allocation failure – no changes were made to @p resp.
 *
 * @note This function performs no logging; log the root cause *before*
 *       calling it if you need diagnostics.
 */
static int respond_400(HttpResponse *resp, const char *msg);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int whoami_json_handler(const HttpRequest *req, HttpResponse *res)
{
    /*-----------------------------------------------------------------------
     * (1)  Compute an ISO‑8601 timestamp with milliseconds.                 *
     *      Use gettimeofday() for µs resolution, convert to UTC, then       *
     *      format as “YYYY‑MM‑DDThh:mm:ss.mmmZ“.                            *
     *---------------------------------------------------------------------*/
    struct timeval tv;       /* wall‑clock with µs */
    gettimeofday(&tv, NULL); /* POSIX; never fails */

    struct tm tm;              /* broken‑down UTC    */
    gmtime_r(&tv.tv_sec, &tm); /* thread‑safe gmtime */

    char timestr[32]; /*  “YYYY‑MM‑DDThh:mm:ss” */
    strftime(timestr, sizeof timestr, "%Y-%m-%dT%H:%M:%S", &tm); /* format up to seconds */

    char iso_time[32]; /* final “…mmmZ” string */
    int len = snprintf(
        iso_time, sizeof iso_time, "%s.%03ldZ",
        /* append “.mmmZ”      */  // NOLINT(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        timestr, tv.tv_usec / 1000L);          /* µs → ms             */
    if(len < 0 || len >= (int)sizeof iso_time) /* buffer overrun?     */
    {
        /* Fallback – keep only the seconds part (better than truncation)   */
        strncpy(iso_time, timestr, sizeof iso_time - 1);
        iso_time[sizeof iso_time - 1] = '\0';
    }

    /*-----------------------------------------------------------------------
     * (2)  Build the JSON structure using cJSON.                           *
     *---------------------------------------------------------------------*/
    cJSON *root = cJSON_CreateObject();                     /* { }                       */
    cJSON_AddStringToObject(root, "server_time", iso_time); /* "server_time": "…"        */
    cJSON_AddStringToObject(root, "method", req->method);   /* "method": "GET"           */
    cJSON_AddStringToObject(root, "path", req->path);       /* "path":   "/api/whoami"   */

    cJSON *hdrs = cJSON_AddObjectToObject(root, "headers"); /* nested object            */
    for(int i = 0; i < req->header_count; ++i)              /* copy every header        */
        cJSON_AddStringToObject(hdrs, req->header_names[i], /* key   */
                                req->header_values[i]);     /* value */

    char *body = cJSON_PrintUnformatted(root); /* compact JSON string      */
    cJSON_Delete(root);                        /* free DOM tree            */

    /*-----------------------------------------------------------------------
     * (3)  Populate the HttpResponse structure for the caller.             *
     *---------------------------------------------------------------------*/
    res->status_code = 200;                 /* HTTP 200 OK              */
    res->status_text = "OK";                /* reason phrase            */
    res->content_type = "application/json"; /* MIME type                */
    res->body = body;                       /* transfer ownership       */
    res->body_length = strlen(body);        /* cache len for sender     */

    return 0; /* success                  */
}

int drive_json_handler(const HttpRequest *req, HttpResponse *resp)
{
    /* ---------- 1. extract & decode the ?path query parameter ---------------- */

    const char *qmark = strchr(req->path, '?');                 /* position of '?' */
    char enc_path[PATH_MAX] = "/";                              /* default = root  */
    if(qmark &&                                                 /* query present ? */
       sscanf(qmark, "?path=%" PATH_MAX_STR "s", enc_path) < 1) /* copy into buf   */
        strcpy(enc_path, "/");                                  /* sscanf failed   */

    char path[PATH_MAX];                            /* decoded result  */
    if(url_decode(enc_path, path, sizeof path) < 0) /* may overflow    */
        respond_400(resp, ("path too long"));
    // return 0; /* early abort     */

    /* ---------- 2. basic sanitisation --------------------------------------- */

    if(strstr(path, ".."))                   /* path traversal? */
        respond_400(resp, ("invalid path")); /* reject          */

    /* ---------- 3. build absolute filesystem path --------------------------- */

    char full[FULLPATH_MAX];                              /* "www" + path    */
    int len = snprintf(full, sizeof full, "www%s", path); /* join components */
    if(len < 0 || len >= (int)sizeof full)                /* buffer overflow */
        respond_400(resp, ("path too long"));

    /* ---------- 4. open the target directory ------------------------------- */

    log_info("Opening dir: %s", full); /* trace           */
    DIR *dir = opendir(full);          /* try to open     */
    if(!dir)                           /* failure?        */
    {
        log_error("drive: opendir failed", strerror(errno)); /* reason in log   */
        resp->status_code = 404;                             /* HTTP Not Found  */
        resp->status_text = "Not Found";
        resp->content_type = "application/json";
        resp->body = strdup("{\"error\":\"not found\"}"); /* tiny payload    */
        resp->body_length = strlen(resp->body);
        return 0; /* done            */
    }

    /* ---------- 5. iterate entries and build JSON --------------------------- */

    cJSON *root = cJSON_CreateObject();                   /* { }             */
    cJSON *items = cJSON_AddArrayToObject(root, "items"); /* "items": [ ]    */
    cJSON_AddStringToObject(root, "path", path);          /* echo request    */

    struct dirent *ent;                 /* iteration var   */
    while((ent = readdir(dir)) != NULL) /* loop dirents    */
    {
        const char *name = ent->d_name;               /* entry name      */
        if(!strcmp(name, ".") || !strcmp(name, "..")) /* skip dots       */
            continue;

        cJSON *obj = cJSON_CreateObject();          /* { } per entry   */
        cJSON_AddStringToObject(obj, "name", name); /* "name": "foo"   */

        /* ---- decide file vs directory ------------------------------------ */

        char child[CHILDFULL_MAX]; /* full child path */
        int l = snprintf(child, sizeof child, "%s/%s", full, name);
        if(l < 0 || l >= (int)sizeof child) /* overflow guard  */
        {
            log_error("drive: child path overflow", name);
            cJSON_Delete(obj); /* discard entry   */
            continue;
        }

        struct stat st;                                  /* stat buffer     */
        if(stat(child, &st) == 0 && S_ISDIR(st.st_mode)) /* directory?      */
            cJSON_AddStringToObject(obj, "type", "directory");
        else /* else file       */
            cJSON_AddStringToObject(obj, "type", "file");

        cJSON_AddItemToArray(items, obj); /* push to "items" */
    }
    closedir(dir); /* release handle  */

    /* ---------- 6. serialise & populate HttpResponse ----------------------- */

    char *json = cJSON_PrintUnformatted(root); /* compact string  */
    cJSON_Delete(root);                        /* free DOM        */

    resp->status_code = 200; /* HTTP OK         */
    resp->status_text = "OK";
    resp->content_type = "application/json";
    resp->body = json; /* transfer owner  */
    resp->body_length = strlen(json);

    return 0; /* success         */
}

int expenses_months_handler(HttpResponse *resp) /* entry‑point for GET /api/expenses/months */
{
    /* ---------- 1. open root directory that holds yearly folders ------------- */

    const char *EXP_ROOT = "www/expenses"; /* constant root path      */
    DIR *d_year = opendir(EXP_ROOT);       /* open "www/expenses"     */
    if(!d_year)                            /* opendir failed ?        */
    {
        resp->status_code = 200;                 /* still HTTP OK           */
        resp->content_type = "application/json"; /* MIME type               */
        resp->body = strdup("[]");               /* empty JSON array        */
        resp->body_length = 2;                   /* strlen("[]")            */
        return 0;                                /* early exit              */
    }

    /* ---------- 2. traverse years and collect “YYYY‑MM” strings -------------- */

    char *months[256]; /* fixed scratch array     */
    int count = 0;     /* number of entries found */

    const struct dirent *ye;      /* iterator: years         */
    while((ye = readdir(d_year))) /* loop over EXP_ROOT      */
    {
        /* ---- accept only directories whose name is exactly four digits ---- */

        if(strlen(ye->d_name) != 4) /* length must be 4        */
            continue;
        int digits = 1;            /* flag: true until fail   */
        for(int i = 0; i < 4; i++) /* validate each char      */
            if(!isdigit((unsigned char)ye->d_name[i])) digits = 0;
        if(!digits) /* skip non‑digit names    */
            continue;

        /* ---- build full path of year directory and validate via stat() ---- */

        char yearpath[PATH_MAX]; /* buffer for path         */
        snprintf(yearpath, sizeof yearpath, "%s/%s", EXP_ROOT, ye->d_name);
        struct stat st;
        if(stat(yearpath, &st) < 0 || !S_ISDIR(st.st_mode)) continue; /* not a directory         */

        /* ---- iterate files inside “YYYY/” looking for “MM.json” ------------ */

        DIR *d_mon = opendir(yearpath); /* open year dir           */
        if(!d_mon) continue;            /* couldn’t open, skip     */

        const struct dirent *me;     /* iterator: months        */
        while((me = readdir(d_mon))) /* loop inside year dir    */
        {
            const char *ext = strrchr(me->d_name, '.'); /* locate last '.'         */
            if(!ext || strcmp(ext, ".json") != 0)       /* require .json suffix    */
                continue;
            if((ext - me->d_name) != 2) /* base name length must 2 */
                continue;
            if(!isdigit((unsigned char)me->d_name[0]) || /* ensure “00”–“99”        */
               !isdigit((unsigned char)me->d_name[1]))
                continue;

            /* ---- build canonical "YYYY‑MM" string ------------------------- */

            char m[8]; /* "YYYY-MM\0"             */
            snprintf(m, sizeof m, "%.4s-%.2s", ye->d_name, me->d_name);
            months[count++] = strdup(m); /* store heap copy         */
        }
        closedir(d_mon); /* close year dir          */
    }
    closedir(d_year); /* close root dir          */

    /* ---------- 3. sort collected strings lexicographically ----------------- */

    qsort(months, count, sizeof months[0], (int (*)(const void *, const void *))strcmp);

    /* ---------- 4. serialise list into compact JSON ------------------------- */

    cJSON *arr = cJSON_CreateArray(); /* new JSON array          */
    for(int i = 0; i < count; i++)    /* push each month         */
    {
        cJSON_AddItemToArray(arr, cJSON_CreateString(months[i]));
        free(months[i]); /* free temp copy          */
    }

    char *out = cJSON_PrintUnformatted(arr); /* "[ ... ]" string        */
    cJSON_Delete(arr);                       /* release DOM             */

    /* ---------- 5. fill HttpResponse ---------------------------------------- */

    resp->status_code = 200;                 /* HTTP OK                 */
    resp->content_type = "application/json"; /* MIME                    */
    resp->body = out;                        /* transfer ownership      */
    resp->body_length = strlen(out);         /* compute size            */
    return 0;                                /* success                 */
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int url_decode(const char *src, char *dst, size_t dst_sz)
{
    size_t di = 0; /* index in dst[]                       */

    /*------------------------------------------------------------------------*
     * Iterate over every byte of the source string until we either run out   *
     * of input (src[si] == '\\0') *or* the destination buffer is one byte    *
     * away from being full (we keep space for the terminating NUL).          *
     *------------------------------------------------------------------------*/
    for(size_t si = 0;                      /* si = source index                    */
        src[si] != '\0' && di < dst_sz - 1; /* stop if input ends or dst almost full*/
        ++si)
    {
        /*---------- pattern “%hh” where both h are hex digits --------------*/
        if(src[si] == '%'                           /*   looking at a '%'                   */
           && isxdigit((unsigned char)src[si + 1])  /*   next char is hex                   */
           && isxdigit((unsigned char)src[si + 2])) /*   char after that is hex             */
        {
            const char hex[3] = {src[si + 1], src[si + 2], '\0'}; /* build \"hh\\0\" for strtol */
            dst[di++] = (char)strtol(hex, NULL, 16); /* convert to byte, store, advance dst  */
            si += 2;                                 /* skip the two hex digits              */
        }
        /*---------- pattern “+” becomes space -----------------------------*/
        else if(src[si] == '+')
        {
            dst[di++] = ' '; /* convert '+' to space                 */
        }
        /*---------- any other byte is copied verbatim ---------------------*/
        else
        {
            dst[di++] = src[si]; /* normal character                     */
        }
    }

    /*------------------------------------------------------------------------*
     * exit the loop either because src was fully consumed or because         *
     * dst is full.  If run out of space, di == dst_sz - 1 and cannot         *
     * NUL‑terminate safely → return error.                                   *
     *------------------------------------------------------------------------*/
    if(di >= dst_sz - 1) /* need room for '\\0'                  */
        return -1;

    dst[di] = '\0'; /* final NUL                            */
    return 0;       /* success                              */
}

static int respond_400(HttpResponse *resp, const char *msg)
{
    const size_t need = strlen(msg) + 20; /* {"error":""} + NUL          */
    char *buf = malloc(need);
    if(!buf) return -1;

    snprintf(buf, need, "{\"error\":\"%s\"}", msg);

    resp->status_code = 400;
    resp->status_text = "Bad Request";
    resp->content_type = "application/json";
    resp->body = buf;
    resp->body_length = strlen(buf);
    return 0;
}
