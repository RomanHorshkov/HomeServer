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
 */

#ifndef SERVER_HANDLER_EXPENSES_H
#define SERVER_HANDLER_EXPENSES_H

#define _GNU_SOURCE

#include "handler_expenses.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "handler_utils.h"
#include "server_settings.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************/
#define EXP_ROOT "www/expenses" /* constant root path */
#define MAX_MONTHS 256          /* max months to collect */

/****************************************************************************
 * PRIVATE FUNCTION PROTOTYPES
 ****************************************************************************/

/* Checks if a directory entry is a valid year (4 digits) */
static int is_valid_year_dir(const struct dirent *de);
/* Checks if a directory entry is a valid month file (MM.json) */
static int is_valid_month_file(const struct dirent *de);
/* Collects all valid months from EXP_ROOT, fills months[] with "YYYY-MM" strings */
static int collect_expense_months(char **months, int max_months);
/* Builds a compact JSON array string from months[] */
static char *build_months_json(char **months, int count);
/****************************************************************************
 * PUBLIC FUNCTION DEFINITIONS
 ****************************************************************************/

// Entry point for /api/expenses (GET: list months, PUT: add expense)
int handler_expenses(const HttpRequest *req, HttpResponse *resp)
{
    if(req->method == HTTP_METHOD_GET)
    {
        // 1. Collect all months with expense files
        char *months[MAX_MONTHS];
        int count = collect_expense_months(months, MAX_MONTHS);
        // 2. Build JSON array string
        char *out = build_months_json(months, count);
        // 3. Fill response
        resp->status_code = 200;
        resp->content_type = "application/json";
        resp->body = out;
        resp->body_length = strlen(out);
        return 0;
    }
    else if(req->method == HTTP_METHOD_PUT)
    {
        // --- Parse JSON body ---
        cJSON *root = cJSON_ParseWithLength(req->body, req->body_len);
        if(!root)
        {
            resp->status_code = 400;
            resp->content_type = "text/plain";
            resp->body = strdup("Invalid JSON");
            resp->body_length = strlen(resp->body);
            return 0;
        }
        // --- Extract fields ---
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
        // --- Parse date to get year/month ---
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
        // --- Build file path ---
        char dirpath[PATH_MAX];
        char monthfile[16];
        char filepath[PATH_MAX];
        snprintf(dirpath, sizeof dirpath, "%s/%s", EXP_ROOT, yearstr);
        snprintf(monthfile, sizeof monthfile, "%s.json", monthstr);
        // Use strncat for extra safety
        if (snprintf(filepath, sizeof filepath, "%s/", dirpath) < (int)sizeof(filepath)) {
            strncat(filepath, monthfile, sizeof(filepath) - strlen(filepath) - 1);
        } else {
            // fallback: ensure null-termination
            filepath[sizeof(filepath)-1] = '\0';
        }
        // --- Ensure directory exists ---
        mkdir(EXP_ROOT, 0775);
        mkdir(dirpath, 0775);
        // --- Read or create file ---
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
        // --- Append new expense ---
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

        // --- Write back to file ---
        // char *out = cJSON_PrintUnformatted(arr);
        char *out = cJSON_Print(arr); // Use pretty-print instead of unformatted
        fseek(f, 0, SEEK_SET);
        fwrite(out, 1, strlen(out), f);
        fflush(f);
        ftruncate(fileno(f), strlen(out));
        fclose(f);
        cJSON_Delete(arr);
        cJSON_Delete(root);
        free(out);
        /* Respond OK */
        resp->status_code = 200;
        resp->content_type = "text/plain";
        resp->body = strdup("OK");
        resp->body_length = 2;
        return 0;
    }
    /* Method not allowed */
    resp->status_code = 405;
    resp->content_type = "text/plain";
    resp->body = strdup("Method Not Allowed");
    resp->body_length = strlen(resp->body);
    return 0;
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

// Checks if a file entry is a valid month file (MM.json)
static int is_valid_month_file(const struct dirent *de)
{
    if(de->d_type != DT_REG) return 0;
    const char *ext = strrchr(de->d_name, '.');
    if(!ext || strcmp(ext, ".json") != 0) return 0;
    if((ext - de->d_name) != 2) return 0;
    if(!isdigit((unsigned char)de->d_name[0]) || !isdigit((unsigned char)de->d_name[1])) return 0;
    return 1;
}

// Scans EXP_ROOT for year/month files, fills months[] with "YYYY-MM" strings, returns count
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

// Builds a compact JSON array string from months[]
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

#endif /* SERVER_HANDLER_EXPENSES_H */
