#define _GNU_SOURCE
#include "router.h"
#include "handlers.h"
#include "static_page.h"
#include "logger.h"
#include <string.h>
#include <stdio.h>
#include <sys/time.h>           // timeval
#include <time.h>               // tm

#define DIR_WWW_INDEX           "www/index.html"
#define DIR_WWW_WHOAMI          "www/whoami.html"
#define DIR_WWW_DYNAMIC         "www/dynamic.html"
#define DIR_WWW_STYLE           "www/style.css"
#define CONTENT_TYPE_HTML       "text/html"
#define CONTENT_TYPE_CSS        "text/css"

#define METHOD_GET              "GET"


/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */


int router_handle_request(const HttpRequest* request, HttpResponse* response)
{
    int res = -1;

    if (request != NULL && response != NULL)
    {
        log_info("req method: %s; req path: %s", request->method, request->path);
        if (strcmp(request->method, METHOD_GET) == 0)
        {
            if (strcmp(request->path, "/") == 0 || strcmp(request->path, "/home") == 0)
            {
                /* Serve index.html as homepage */
                res = static_page_serve_file(DIR_WWW_INDEX, CONTENT_TYPE_HTML, response);
            }
            else if (strcmp(request->path, "/style.css") == 0)
            {
                /* Serve the CSS file */
                res = static_page_serve_file(DIR_WWW_STYLE, CONTENT_TYPE_CSS, response);
            }
            else if (strcmp(request->path, "/whoami") == 0)
            {
                // new: serve the static HTML page
                res =  static_page_serve_file(DIR_WWW_WHOAMI, CONTENT_TYPE_HTML, response);
            }
            else if (strcmp(request->path, "/dynamic") == 0)
            {
                // new: serve the static HTML page
                res =  static_page_serve_file(DIR_WWW_DYNAMIC, CONTENT_TYPE_HTML, response);
            }
            else if (strcmp(request->path, "/api/whoami") == 0)
            {
                res =  whoami_json_handler(request, response);
            }
            // else if (strcmp(request->path, "/whoami") == 0)
            // {
            //     // bump up HTML buffer so we can include all headers
            //     static char html[8192];
            //     static char time_str[64];
            //     static char full_time_str[128];
            //     struct timeval tv;
            //     gettimeofday(&tv, NULL);
            //     struct tm tm;
            //     gmtime_r(&tv.tv_sec, &tm);
            //     strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", &tm);
            //     snprintf(full_time_str, sizeof(full_time_str),
            //              "%s.%03ldZ", time_str, tv.tv_usec / 1000);
            
            //     // start writing HTML
            //     int len = snprintf(html, sizeof(html),
            //         "<!DOCTYPE html>"
            //         "<html><head><title>Who Am I</title>"
            //         "<link rel=\"stylesheet\" href=\"/style.css\">"
            //         "<script>"
            //           "let serverTime = new Date('%s');"
            //           "function updateClock() {"
            //           "  serverTime = new Date(serverTime.getTime() + 1000);"
            //           "  document.getElementById('clock').textContent = "
            //                  "serverTime.toLocaleString();"
            //           "}"
            //           "setInterval(updateClock, 1000);"
            //           "window.onload = updateClock;"
            //         "</script>"
            //         "</head><body>"
            //         "<h1>Who Am I?</h1>"
            //         "<p><strong>Live server time:</strong></p>"
            //         "<div id='clock' style='font-size:1.5rem; margin-bottom:1em;'></div>"
            //         "<h2>Request Info</h2>"
            //         "<p><strong>Method:</strong> %s<br>"
            //         "<strong>Path:</strong> %s</p>"
            //         "<h3>Headers</h3>"
            //         "<ul>",
            //         full_time_str,
            //         request->method,
            //         request->path
            //     );
            
            //     // append each header
            //     for (int i = 0; i < request->header_count && len < (int)sizeof(html); ++i) {
            //         len += snprintf(html + len, sizeof(html) - len,
            //                         "<li>%s: %s</li>",
            //                         request->header_names[i],
            //                         request->header_values[i]);
            //     }
            
            //     // close HTML
            //     snprintf(html + len, sizeof(html) - len,
            //         "</ul>"
            //         "</body></html>"
            //     );
            
            //     // fill out the response
            //     response->status_code   = 200;
            //     response->status_text   = "OK";
            //     response->content_type  = "text/html";
            //     response->body          = html;
            //     response->body_length   = strlen(html);
            
            //     res = 0;
            // }
            
            else
            {
                /* 404 Not Found */
                response->status_code = 404;
                response->status_text = "Not Found";
                response->content_type = CONTENT_TYPE_HTML;
                response->body = "<html><body><h1>404 Not Found</h1></body></html>";
                response->body_length = strlen(response->body);
                res = 0;
            }
        }
        else
        {
            /* 405 Method Not Allowed */
            response->status_code = 405;
            response->status_text = "Method Not Allowed";
            response->content_type = CONTENT_TYPE_HTML;
            response->body = "<html><body><h1>405 Method Not Allowed</h1></body></html>";
            response->body_length = strlen(response->body);
            res = 0;
        }
    }

    return res;
}
