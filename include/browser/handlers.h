#include "router.h"

int whoami_json_handler(const HttpRequest* req, HttpResponse* res);

int drive_json_handler(const HttpRequest* req, HttpResponse* resp);

int expenses_months_handler(HttpResponse* resp);

// void expenses_add_handler(const HttpRequest*, HttpResponse*);