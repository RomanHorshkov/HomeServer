/*
 * handlers.c
 * ----------
 * Endpoint handlers for the tiny web‑server:
 *   - /api/whoami    -> JSON with request echo & server time
 *   - /api/drive     -> JSON directory listing for the virtual “Drive”
 *
 * The file also provides a minimal percent‑decoder for URL paths.
 *
 * (c) 2025 Your Name
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

/* ---------------------------------------------------------------------------
 * Local constants & helper macros
 * -------------------------------------------------------------------------*/

/* stringify PATH_MAX to build “%<PATH_MAX>s” format strings with sscanf()   */
#define _STR(x) #x
#define _XSTR(x) _STR(x)
#define PATH_MAX_STR _XSTR(PATH_MAX)

/* Allow room for “www” prefix + path + NUL                                   */
#define FULLPATH_MAX (PATH_MAX + 4)
/* Allow room for fullpath + “/” + NAME_MAX + NUL                             */
#define CHILDFULL_MAX (FULLPATH_MAX + NAME_MAX + 1)

/* ---------- Shortcuts to build JSON error replies ------------------------ */

/**
 * respond_400()
 * -------------
 * Fill @resp with status 400 and a tiny JSON body `{"error":"<msg>"}`.
 * Returns 0 on success, ‑1 on allocation failure.
 */
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

/* Convenience macro that bails out of the current handler with a 400 reply. */
#define RESP_400(MSG)             \
    do                            \
    {                             \
        respond_400(resp, (MSG)); \
        return 0;                 \
    } while(0)

/* ---------------------------------------------------------------------------
 * Private prototypes
 * -------------------------------------------------------------------------*/
static int url_decode(const char *src, char *dst, size_t dst_sz);

/* ---------------------------------------------------------------------------
 * Public handlers
 * -------------------------------------------------------------------------*/

/**
 * whoami_json_handler()
 * ---------------------
 * Echo endpoint used for diagnostics.
 * Produces:
 *   {
 *     "server_time":"2025-05-03T20:11:22.123Z",
 *     "method":"GET",
 *     "path":"/api/whoami",
 *     "headers":{ ... }
 *   }
 */
int whoami_json_handler(const HttpRequest *req, HttpResponse *res)
{
    /* --- (1) Local ISO‑8601 timestamp with milliseconds ------------------ */
    struct timeval tv;
    gettimeofday(&tv, NULL);

    struct tm tm;
    gmtime_r(&tv.tv_sec, &tm);

    char timestr[32];
    strftime(timestr, sizeof timestr, "%Y-%m-%dT%H:%M:%S", &tm);

    char iso_time[32];
    int len = snprintf(iso_time, sizeof iso_time, "%s.%03ldZ", timestr, tv.tv_usec / 1000L);
    if(len < 0 || len >= (int)sizeof iso_time) /* safety fallback   */
        strncpy(iso_time, timestr, sizeof iso_time - 1), iso_time[sizeof iso_time - 1] = '\0';

    /* --- (2) Build JSON -------------------------------------------------- */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "server_time", iso_time);
    cJSON_AddStringToObject(root, "method", req->method);
    cJSON_AddStringToObject(root, "path", req->path);

    cJSON *hdrs = cJSON_AddObjectToObject(root, "headers");
    for(int i = 0; i < req->header_count; ++i)
        cJSON_AddStringToObject(hdrs, req->header_names[i], req->header_values[i]);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    /* --- (3) Fill HttpResponse ------------------------------------------ */
    res->status_code = 200;
    res->status_text = "OK";
    res->content_type = "application/json";
    res->body = body;
    res->body_length = strlen(body);

    return 0;
}

/**
 * drive_json_handler()
 * --------------------
 * Implements GET /api/drive?path=/some/dir
 * Returns a JSON listing of the directory contents below the project’s
 * “www/” folder.
 */
int drive_json_handler(const HttpRequest *req, HttpResponse *resp)
{
    /* --- (1) Extract and URL‑decode the `path` query param -------------- */
    const char *qmark = strchr(req->path, '?');
    char enc_path[PATH_MAX] = "/";
    if(qmark && sscanf(qmark, "?path=%" PATH_MAX_STR "s", enc_path) < 1) strcpy(enc_path, "/");

    char path[PATH_MAX];
    if(url_decode(enc_path, path, sizeof path) < 0) RESP_400("path too long");

    /* --- (2) Basic sanitisation ---------------------------------------- */
    if(strstr(path, "..")) RESP_400("invalid path");

    /* --- (3) Build absolute path on disk ("www" + path) ----------------- */
    char full[FULLPATH_MAX];
    int len = snprintf(full, sizeof full, "www%s", path);
    if(len < 0 || len >= (int)sizeof full) RESP_400("path too long");

    /* --- (4) Open directory -------------------------------------------- */
    log_info("Opening dir: %s", full);
    DIR *dir = opendir(full);
    if(!dir)
    {
        log_error("drive: opendir failed", strerror(errno));
        resp->status_code = 404;
        resp->status_text = "Not Found";
        resp->content_type = "application/json";
        resp->body = strdup("{\"error\":\"not found\"}");
        resp->body_length = strlen(resp->body);
        return 0;
    }

    /* --- (5) Build JSON listing ---------------------------------------- */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "path", path);
    cJSON *items = cJSON_AddArrayToObject(root, "items");

    struct dirent *ent;
    while((ent = readdir(dir)) != NULL)
    {
        const char *name = ent->d_name;
        if(!strcmp(name, ".") || !strcmp(name, "..")) continue;

        /* create JSON object for this entry */
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "name", name);

        /* Decide “file” vs “directory” with stat() */
        char child[CHILDFULL_MAX];
        int l = snprintf(child, sizeof child, "%s/%s", full, name);
        if(l < 0 || l >= (int)sizeof child)
        {
            log_error("drive: child path overflow", name);
            cJSON_Delete(obj);
            continue;
        }

        struct stat st;
        if(stat(child, &st) == 0 && S_ISDIR(st.st_mode))
            cJSON_AddStringToObject(obj, "type", "directory");
        else
            cJSON_AddStringToObject(obj, "type", "file");

        cJSON_AddItemToArray(items, obj);
    }
    closedir(dir);

    /* --- (6) Serialise & send ------------------------------------------ */
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    resp->status_code = 200;
    resp->status_text = "OK";
    resp->content_type = "application/json";
    resp->body = json;
    resp->body_length = strlen(json);

    return 0;
}

/**
 * expenses_data_handler()
 * -----------------------
 * GET /api/expenses/data
 * Scans the “expenses/” directory for per-month JSON files and returns:
 *   {
 *     "2025-01": 345.60,
 *     "2025-02": 412.00,
 *     …
 *   }
 */
int expenses_data_handler(HttpResponse *resp)
{
    const char *EXP_ROOT = "www/expenses";
    DIR *d_year = opendir(EXP_ROOT);
    if(!d_year)
    {
        log_error("expenses: could not open root dir '%s': %s", EXP_ROOT, strerror(errno));
        // Return empty object instead of error
        resp->status_code = 200;
        resp->status_text = "OK";
        resp->content_type = "application/json";
        resp->body = strdup("{}");
        resp->body_length = 2;
        return 0;
    }

    cJSON *root = cJSON_CreateObject();

    struct dirent *year_ent;
    while((year_ent = readdir(d_year)) != NULL)
    {
        const char *year = year_ent->d_name;
        // skip “.”, “..” and non-4-digit names
        if(year[0] == '.' || strlen(year) != 4) continue;
        for(int i = 0; i < 4; ++i)
            if(!isdigit((unsigned char)year[i])) goto next_year;

        {
            char year_path[PATH_MAX];
            snprintf(year_path, sizeof year_path, "%s/%s", EXP_ROOT, year);

            struct stat st;
            if(stat(year_path, &st) < 0 || !S_ISDIR(st.st_mode))
            {
                log_info("expenses: skipping non-dir %s", year_path);
                goto next_year;
            }

            DIR *d_month = opendir(year_path);
            if(!d_month)
            {
                log_error("expenses: cannot open %s: %s", year_path, strerror(errno));
                goto next_year;
            }

            struct dirent *mon_ent;
            while((mon_ent = readdir(d_month)) != NULL)
            {
                const char *fname = mon_ent->d_name;
                /* look for “MM.json” */
                const char *ext = strrchr(fname, '.');
                if(!ext || strcmp(ext, ".json") != 0) continue;
                if((ext - fname) != 2) continue;
                if(!isdigit((unsigned char)fname[0]) || !isdigit((unsigned char)fname[1])) continue;

                /* make room for “expenses/YYYY/MM.json” safely */
                char fullpath[CHILDFULL_MAX];
                int ret = snprintf(fullpath, sizeof fullpath, "%s/%s", year_path, fname);
                if(ret < 0 || (size_t)ret >= sizeof fullpath)
                {
                    log_error("expenses: fullpath overflow (%d >= %zu)", ret, sizeof fullpath);
                    continue;
                }

                /* read entire file */
                FILE *f = fopen(fullpath, "r");
                if(!f)
                {
                    log_error("expenses: fopen %s failed: %s", fullpath, strerror(errno));
                    continue;
                }
                fseek(f, 0, SEEK_END);
                long sz = ftell(f);
                rewind(f);
                char *buf = malloc(sz + 1);
                if(!buf)
                {
                    fclose(f);
                    continue;
                }
                fread(buf, 1, sz, f);
                buf[sz] = '\0';
                fclose(f);

                /* parse JSON array */
                cJSON *arr = cJSON_Parse(buf);
                free(buf);
                if(!arr || !cJSON_IsArray(arr))
                {
                    cJSON_Delete(arr);
                    log_error("expenses: invalid JSON array in %s", fullpath);
                    continue;
                }

                /* sum amounts */
                double total = 0.0;
                cJSON *item;
                cJSON_ArrayForEach(item, arr)
                {
                    cJSON *amt = cJSON_GetObjectItem(item, "amount");
                    if(cJSON_IsNumber(amt)) total += amt->valuedouble;
                }
                cJSON_Delete(arr);

                /* build key "YYYY-MM" (`year` is exactly 4 digits) */
                char key[8];
                ret = snprintf(key, sizeof key, "%.4s-%2.2s", year, fname);
                if(ret < 0 || ret >= (int)sizeof key)
                {
                    log_error("expenses: could not format month key (%d >= %zu)", ret, sizeof key);
                    continue;
                }

                cJSON_AddNumberToObject(root, key, total);
            }

            closedir(d_month);
        }
    next_year:;
    }

    closedir(d_year);

    // serialize and respond
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    resp->status_code = 200;
    resp->status_text = "OK";
    resp->content_type = "application/json";
    resp->body = out;
    resp->body_length = strlen(out);

    return 0;
}

/* ---------------------------------------------------------------------------
 * Private helpers
 * -------------------------------------------------------------------------*/

/**
 * url_decode()
 * ------------
 * Percent‑decodes @src into @dst, stopping early if @dst_sz would overflow.
 * Returns 0 on success, ‑1 if the output would not fit.
 *
 * Rules:
 *   %xx   → byte with hex value xx
 *   '+'   → space
 *   other → unchanged
 */
static int url_decode(const char *src, char *dst, size_t dst_sz)
{
    size_t di = 0;
    for(size_t si = 0; src[si] && di < dst_sz - 1; ++si)
    {
        if(src[si] == '%' && isxdigit((unsigned char)src[si + 1]) &&
           isxdigit((unsigned char)src[si + 2]))
        {
            const char hex[3] = {src[si + 1], src[si + 2], '\0'};
            dst[di++] = (char)strtol(hex, NULL, 16);
            si += 2;
        }
        else if(src[si] == '+')
        {
            dst[di++] = ' ';
        }
        else
        {
            dst[di++] = src[si];
        }
    }

    if(di >= dst_sz - 1) /* no room for final NUL */
        return -1;

    dst[di] = '\0';
    return 0;
}
