/**
 * @file folder_zip_pump.h
 * @brief Streams a folder as a live ZIP (see folder_zip_pump.c). Runs on the upload pool; entered from
 *        client_upload_pump when the gate claims GET /api/app/files/folders/zip/<token>.
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 */
#ifndef DB_SERVER_CORE_WORKER_OPERATOR_CLIENT_FOLDER_ZIP_PUMP_H
#define DB_SERVER_CORE_WORKER_OPERATOR_CLIENT_FOLDER_ZIP_PUMP_H

#include <DB_http/DB_http.h>

#include <db_app.h>
#include <db_server/core/worker/operator/client/client.h>

/** @brief 1 when @p path is under "/api/app/files/folders/zip/" (with a token), else 0. */
int folder_zip_is_zip_path(sv_t path);

/** @brief Stream the archive for the token in @p req->path to @p cli. Sends the whole HTTP response itself; connection closes after. */
int client_folder_zip_pump(client_t* cli, DB_app_request_t* req);

#endif /* DB_SERVER_CORE_WORKER_OPERATOR_CLIENT_FOLDER_ZIP_PUMP_H */
