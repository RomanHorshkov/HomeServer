#define _GNU_SOURCE
#include "handlers.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "cJSON.h"

int whoami_json_handler(const HttpRequest *req, HttpResponse *res)
{
    // 1) get server time with millisecond precision
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    gmtime_r(&tv.tv_sec, &tm);

    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y-%m-%dT%H:%M:%S", &tm);

    char full_time[128];
    snprintf(full_time, sizeof(full_time), "%s.%03ldZ", timestr, tv.tv_usec / 1000);

    // 2) build JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "server_time", full_time);
    cJSON_AddStringToObject(root, "method", req->method);
    cJSON_AddStringToObject(root, "path", req->path);

    cJSON *hdrs = cJSON_CreateObject();
    for(int i = 0; i < req->header_count; i++)
    {
        cJSON_AddStringToObject(hdrs, req->header_names[i], req->header_values[i]);
    }
    cJSON_AddItemToObject(root, "headers", hdrs);

    // 3) serialize
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    // 4) fill response
    res->status_code = 200;
    res->status_text = "OK";
    res->content_type = "application/json";
    res->body = body;
    res->body_length = strlen(body);

    /* TO DO : FREE THE BODY AFTER USE!!! */

    return 0;
}
