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
 * Handles the **GET /api/expenses/months** endpoint by scanning the directory
 * structure:
 *   www/expenses/YYYY/MM.json
 * and collecting all (YYYY, MM) pairs that match the pattern.
 *
 * The response is a sorted JSON array of strings, e.g.:
 *   ["2023-11", "2024-02", "2025-01"]
 *
 * No request parameters are required.
 *
 * Ownership:
 *   The response body buffer is allocated using cJSON_PrintUnformatted().
 *   The caller is responsible for freeing it with free().
 *
 * @param[in]  req   Pointer to the parsed HttpRequest (unused).
 * @param[out] resp  Pointer to the HttpResponse to populate.
 *
 * @retval  0  Success – resp is populated (HTTP 200).
 * @retval -1  Fatal error (e.g., memory allocation failure).
 */
int handler_expenses(const HttpRequest *req, HttpResponse *resp);

#endif /* SERVER_BROWSER_HANDLER_EXPENSES_H */
