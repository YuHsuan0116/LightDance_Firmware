#include "http_server.h"

#include "esp_check.h"

static const char* TAG = "http_server";

httpd_handle_t s_server = NULL;

static esp_err_t open_handler(httpd_handle_t server, int sockfd) {
    int enable = 1;

    ESP_RETURN_ON_FALSE(setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) == 0,
                        ESP_FAIL, TAG, "failed to enable TCP_NODELAY: errno=%d (%s)", errno,
                        strerror(errno));

    ESP_LOGI(TAG, "New TCP connection: %d", sockfd);

    return ESP_OK;
}

esp_err_t http_server_run() {
    ESP_RETURN_ON_FALSE(s_server == NULL, ESP_ERR_INVALID_STATE, TAG, "Server already started");
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.open_fn = open_handler;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "Failed to start HTTP server");

    return ESP_OK;
}

esp_err_t http_server_register_get(const char* uri, http_handler handler, void* user_ctx) {
    ESP_RETURN_ON_FALSE(s_server != NULL, ESP_ERR_INVALID_STATE, TAG, "Server not started");

    httpd_uri_t uri_handler = {0};
    uri_handler.uri = uri;
    uri_handler.method = HTTP_GET;
    uri_handler.handler = handler;
    uri_handler.user_ctx = user_ctx;

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &uri_handler), TAG,
                        "Failed to register GET handler");

    return ESP_OK;
}
esp_err_t http_server_register_post(const char* uri, http_handler handler, void* user_ctx) {
    ESP_RETURN_ON_FALSE(s_server != nullptr, ESP_ERR_INVALID_STATE, TAG, "Server not started");

    httpd_uri_t uri_handler = {0};
    uri_handler.uri = uri;
    uri_handler.method = HTTP_POST;
    uri_handler.handler = handler;
    uri_handler.user_ctx = user_ctx;

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &uri_handler), TAG,
                        "Failed to register POST handler");

    return ESP_OK;
}
