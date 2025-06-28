/*
 * handler_expenses.c
 * ------------------
 * Implements the /api/expenses endpoint handler for the web server.
 *
 * - GET: Responds with a JSON array of "YYYY-MM" strings, representing months
 *        for which expense records exist under www/expenses/<year>/<month>.json.
 * - (Future) PUT/POST: Add new expense records (not implemented here).
 *
 * (c) 2025 Roman Horshkov
 *
 */

#define _GNU_SOURCE

#include "handler_expenses.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "handler_utils.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************/

#define EXP_ROOT "/home/roman/HomeServer/var/lib/expenses"

#define SETTINGS_FILE "/home/roman/HomeServer/var/lib/expenses/settings.json"
#define MAX_MONTHS 256 /* max months to collect */

/****************************************************************************
 * PRIVATE FUNCTION PROTOTYPES
 ****************************************************************************/

/**
 * @brief Checks if a directory entry is a valid year directory (format: YYYY).
 * @param de Pointer to directory entry.
 * @return 1 if valid year directory, 0 otherwise.
 */
static int is_valid_year_dir(const struct dirent *de);

/**
 * @brief Checks if a directory entry is a valid month file (format: MM.json).
 * @param de Pointer to directory entry.
 * @return 1 if valid month file, 0 otherwise.
 */
static int is_valid_month_file(const struct dirent *de);

/**
 * @brief Collects all valid months from EXP_ROOT, fills months[] with "YYYY-MM" strings.
 * @param months Output array of strings (allocated with strdup).
 * @param max_months Maximum number of months to collect.
 * @return Number of months found.
 */
static int collect_expense_months(char **months, int max_months);

/**
 * @brief Builds a compact JSON array string from months[].
 * @param months Array of "YYYY-MM" strings.
 * @param count Number of months in the array.
 * @return Pointer to a heap-allocated JSON string (must be freed by caller).
 */
static char *build_months_json(char **months, int count);

/**
 * @brief Helper to serve a static JSON file from EXP_ROOT or absolute path.
 * @param filename Absolute or relative path to JSON file.
 * @param resp Pointer to HttpResponse to populate.
 * @return STATUS_SUCCESS on success, STATUS_FAILURE on error.
 */
static int serve_static_json(const char *filename, HttpResponse *resp);

/****************************************************************************
 * PUBLIC FUNCTION DEFINITIONS
 ****************************************************************************/

int handler_expenses(const HttpRequest *req, HttpResponse *resp)
{
    /* result variable */
    int res = STATUS_FAILURE;

    log_info("[handler_expenses]: called with method %d, path %s", req->method, req->path);
    // ^^^ Add path to log for easier debugging

    switch(req->method)
    {
        case HTTP_METHOD_GET:
        {
            res = STATUS_SUCCESS;

            /* Serve the settings file if requested */
            if(strcmp(req->path, "/api/expenses/settings.json") == 0)
            {
                // Serve the static settings.json file
                return serve_static_json(SETTINGS_FILE, resp);
            }
            /* List all available months if requesting the root endpoint */
            else if(strcmp(req->path, "/api/expenses/") == 0 ||
                    strcmp(req->path, "/api/expenses") == 0)
            {
                /* Collect all months with expense files */
                char *months[MAX_MONTHS];
                int count = collect_expense_months(months, MAX_MONTHS);

                // /* Build a compact JSON array string */
                char *out = build_months_json(months, count);

                /* Fill the response */
                resp->status_code = 200;
                resp->content_type = "application/json";
                resp->body = out;
                resp->body_length = strlen(out);
            }
            /* Serve a specific month file: /api/expenses/YYYY/MM.json */
            else
            {
                /* Try to match the pattern /api/expenses/YYYY/MM.json */
                int year, month;
                char extra[32];
                /* sscanf returns 2 if it matches exactly "/api/expenses/YYYY/MM.json" */
                if(sscanf(req->path, "/api/expenses/%4d/%2d.json%31s", &year, &month, extra) == 2)
                {
                    /* Build the file path to the requested month */
                    char filepath[PATH_MAX];
                    snprintf(filepath, sizeof(filepath), "%s/%04d/%02d.json", EXP_ROOT, year,
                             month);

                    /* Serve the static JSON file for the requested month */
                    return serve_static_json(filepath, resp);
                }

                // 4. If none of the above, return 404 Not Found
                resp->status_code = 404;
                resp->content_type = "text/plain";
                resp->body = strdup("Not found");
                resp->body_length = strlen(resp->body);
                res = STATUS_FAILURE;
            }
            break;
        }
        case HTTP_METHOD_PUT:
        {
            /* Parse JSON body */
            cJSON *root = cJSON_ParseWithLength(req->body, req->body_len);
            if(!root)
            {
                resp->status_code = 400;
                resp->content_type = "text/plain";
                resp->body = strdup("Invalid JSON");
                resp->body_length = strlen(resp->body);
                return 0;
            }

            /* Extract fields */
            const cJSON *date = cJSON_GetObjectItem(root, "date");
            const cJSON *category = cJSON_GetObjectItem(root, "category");
            const cJSON *amount = cJSON_GetObjectItem(root, "amount");
            const cJSON *comment = cJSON_GetObjectItem(root, "comment");
            if(!cJSON_IsString(date) || !cJSON_IsString(category) || !cJSON_IsNumber(amount))
            {
                cJSON_Delete(root);
                resp->status_code = 400;
                resp->content_type = "text/plain";
                resp->body = strdup("Missing or invalid fields");
                resp->body_length = strlen(resp->body);
                return 0;
            }

            /* Parse date to get year/month */
            int d, m, y;
            if(sscanf(date->valuestring, "%d/%d/%d", &d, &m, &y) != 3)
            {
                cJSON_Delete(root);
                resp->status_code = 400;
                resp->content_type = "text/plain";
                resp->body = strdup("Invalid date format");
                resp->body_length = strlen(resp->body);
                return 0;
            }
            char yearstr[8], monthstr[4];
            snprintf(yearstr, sizeof yearstr, "%04d", y);
            snprintf(monthstr, sizeof monthstr, "%02d", m);

            /* Build file path */
            char dirpath[PATH_MAX];
            char monthfile[16];
            char filepath[PATH_MAX];
            snprintf(dirpath, sizeof dirpath, "%s/%s", EXP_ROOT, yearstr);
            snprintf(monthfile, sizeof monthfile, "%s.json", monthstr);

            /* Use strncat for extra safety */
            if(snprintf(filepath, sizeof filepath, "%s/", dirpath) < (int)sizeof(filepath))
            {
                strncat(filepath, monthfile, sizeof(filepath) - strlen(filepath) - 1);
            }
            else
            {
                /* fallback: ensure null-termination */
                filepath[sizeof(filepath) - 1] = '\0';
            }

            /* Ensure directory exists */
            mkdir(EXP_ROOT, 0775);
            mkdir(dirpath, 0775);

            /* Read or create file */
            FILE *f = fopen(filepath, "r+");
            cJSON *arr = NULL;
            if(f)
            {
                fseek(f, 0, SEEK_END);
                long len = ftell(f);
                fseek(f, 0, SEEK_SET);
                char *buf = malloc(len + 1);
                fread(buf, 1, len, f);
                buf[len] = 0;
                arr = cJSON_Parse(buf);
                free(buf);
                if(!cJSON_IsArray(arr))
                {
                    cJSON_Delete(arr);
                    arr = cJSON_CreateArray();
                }
            }
            else
            {
                arr = cJSON_CreateArray();
                f = fopen(filepath, "w+");
            }
            /* Append new expense */
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddStringToObject(obj, "date", date->valuestring);
            cJSON_AddStringToObject(obj, "category", category->valuestring);
            cJSON_AddNumberToObject(obj, "amount", amount->valuedouble);
            if(cJSON_IsString(comment))
            {
                cJSON_AddStringToObject(obj, "comment", comment->valuestring);
            }
            else
            {
                cJSON_AddStringToObject(obj, "comment", "");
            }
            cJSON_AddItemToArray(arr, obj);

            /* Write back to file */
            char *out2 = cJSON_Print(arr);  // Use pretty-print instead of unformatted
            fseek(f, 0, SEEK_SET);
            fwrite(out2, 1, strlen(out2), f);
            fflush(f);
            ftruncate(fileno(f), strlen(out2));
            fclose(f);
            cJSON_Delete(arr);
            cJSON_Delete(root);
            free(out2);

            /* Respond OK */
            resp->status_code = 200;
            resp->content_type = "text/plain";
            resp->body = strdup("OK");
            resp->body_length = 2;

            res = STATUS_SUCCESS;
            break;
        }
        case HTTP_METHOD_POST:
        {
            /* Not implemented: future support for adding expenses */
            resp->status_code = 501;
            resp->content_type = "text/plain";
            resp->body = strdup("Not implemented");
            resp->body_length = strlen(resp->body);
            break;
        }
        case HTTP_METHOD_DELETE:
        case HTTP_METHOD_UNKNOWN:
        default:
            /* Method not allowed */
            resp->status_code = 405;
            resp->content_type = "text/plain";
            resp->body = strdup("Method Not Allowed");
            resp->body_length = strlen(resp->body);
            break;
    }

    return res;
}

/****************************************************************************
 * PRIVATE FUNCTION DEFINITIONS
 ***************************************************************************
 */

static int is_valid_year_dir(const struct dirent *de)
{
    if(de->d_type != DT_DIR) return 0;
    if(strlen(de->d_name) != 4) return 0;
    for(int i = 0; i < 4; i++)
        if(!isdigit((unsigned char)de->d_name[i])) return 0;
    return 1;
}

static int is_valid_month_file(const struct dirent *de)
{
    if(de->d_type != DT_REG) return 0;
    const char *ext = strrchr(de->d_name, '.');
    if(!ext || strcmp(ext, ".json") != 0) return 0;
    if((ext - de->d_name) != 2) return 0;
    if(!isdigit((unsigned char)de->d_name[0]) || !isdigit((unsigned char)de->d_name[1])) return 0;
    return 1;
}

static int collect_expense_months(char **months, int max_months)
{
    DIR *d_year = opendir(EXP_ROOT);
    if(!d_year) return 0;
    int count = 0;
    struct dirent *ye;
    while((ye = readdir(d_year)))
    {
        if(!is_valid_year_dir(ye)) continue;
        char yearpath[PATH_MAX];
        snprintf(yearpath, sizeof yearpath, "%s/%s", EXP_ROOT, ye->d_name);
        DIR *d_mon = opendir(yearpath);
        if(!d_mon) continue;
        struct dirent *me;
        while((me = readdir(d_mon)))
        {
            if(!is_valid_month_file(me)) continue;
            char m[8];
            snprintf(m, sizeof m, "%.4s-%.2s", ye->d_name, me->d_name);
            if(count < max_months) months[count++] = strdup(m);
        }
        closedir(d_mon);
    }
    closedir(d_year);
    return count;
}

static char *build_months_json(char **months, int count)
{
    qsort(months, count, sizeof months[0], (int (*)(const void *, const void *))strcmp);
    cJSON *arr = cJSON_CreateArray();
    for(int i = 0; i < count; i++)
    {
        cJSON_AddItemToArray(arr, cJSON_CreateString(months[i]));
        free(months[i]);
    }
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return out;
}

static int serve_static_json(const char *filename, HttpResponse *resp)
{
    char path[PATH_MAX];
    // If filename is absolute, use as is; else prepend EXP_ROOT
    if(filename[0] == '/')
    {
        snprintf(path, sizeof path, "%s", filename);
    }
    else
    {
        snprintf(path, sizeof path, "%s/%s", EXP_ROOT, filename);
    }
    FILE *f = fopen(path, "rb");
    if(!f)
    {
        resp->status_code = 404;
        resp->content_type = "text/plain";
        resp->body = strdup("Not found");
        resp->body_length = strlen(resp->body);
        return STATUS_FAILURE;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = 0;
    fclose(f);

    resp->status_code = 200;
    resp->content_type = "application/json";
    resp->body = buf;
    resp->body_length = len;
    return STATUS_SUCCESS;
}
