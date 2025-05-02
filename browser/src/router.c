#include "router.h"
#include "static_page.h"
#include <string.h>

#define DIR_WWW_INDEX       "www/index.html"
#define DIR_WWW_STYLE       "www/style.css"
#define CONTENT_TYPE_HTML   "text/html"
#define CONTENT_TYPE_CSS    "text/css"

#define METHOD_GET          "GET"


/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */


int router_handle_request(const HttpRequest* request, HttpResponse* response)
{
    int res = -1;

    if (request != NULL && response != NULL)
    {
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
