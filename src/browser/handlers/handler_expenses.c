/*
 * handler_expenses.c
 * ------------------
 * Implements the /api/expenses/months endpoint handler for the web server.
 *
 * Responds with a JSON array of "YYYY-MM" strings, representing months
 * for which expense records exist under www/expenses/<year>/<month>.json.
 *
 * Traverses the expenses directory, collects valid year/month pairs,
 * sorts them, and serializes the result as a compact JSON array.
 *
 * Each handler receives a parsed HttpRequest and fills a fresh HttpResponse.
 * No socket I/O is performed here; the caller is responsible for network
 * transmission and memory cleanup.
 *
 * (c) 2025 Roman Horshkov
 */

#define _GNU_SOURCE

#include "handler_expenses.h"

#include "handler_utils.h"
#include "server_settings.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int handler_expenses(HttpResponse *resp) /* entry‑point for GET /api/expenses/months */
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
/* None */
