#ifndef SERVER_BROWSER_HANDLER_EXPENSES_H
#define SERVER_BROWSER_HANDLER_EXPENSES_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */

#include "http_manager.h" /* HttpRequest, HttpResponse */

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
 */
/* None */

/**
 * PUBLIC FUNCTIONS DECLARATIONS
 */

/**
 * @brief Enumerate every month for which an expense JSON file exists.
 *
 * The endpoint services **GET /api/expenses/months** and scans the directory
 * tree
 * ~~~text
 *   www/expenses/YYYY/MM.json
 * ~~~
 * collecting all pairs ( `YYYY`, `MM` ) that match the pattern.
 * The response is a sorted JSON array, e.g.
 * ~~~json
 * ["2023‑11","2024‑02","2025‑01"]
 * ~~~
 *
 * *No request parameters are required.*
 *
 * Ownership rules
 * ---------------
 * The function allocates the body buffer with `cJSON_PrintUnformatted()`;
 * the caller (server core) becomes responsible for `free()`‑ing it.
 *
 * @param[out] resp  Pre‑allocated response object to fill.
 *
 * @retval 0  Success – @p resp is populated (always HTTP 200).
 * @retval -1 Fatal error (memory allocation failure).
 */
int handler_expenses(HttpResponse* resp);

#endif /* SERVER_BROWSER_HANDLER_EXPENSES_H */
