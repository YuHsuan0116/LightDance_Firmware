#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*http_handler)(httpd_req_t* req);

esp_err_t http_server_run();

esp_err_t http_server_register_get(const char* uri, http_handler handler, void* user_ctx);
esp_err_t http_server_register_post(const char* uri, http_handler handler, void* user_ctx);

#ifdef __cplusplus
}
#endif
