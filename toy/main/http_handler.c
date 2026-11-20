#include "http_handler.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "app.h"
#include "http_server.h"
#include "scheduler.h"

#include "cJSON.h"

static const char* TAG = "http_handler";

static cJSON* recv_json(httpd_req_t* req) {
    char buf[256];
    size_t size = req->content_len;

    if (size == 0 || size >= sizeof(buf)) {
        return NULL;
    }

    size_t received = 0;
    while (received < size) {
        int ret = httpd_req_recv(req, buf + received, size - received);
        if (ret <= 0)
            return NULL;
        received += ret;
    }

    buf[received] = '\0';
    return cJSON_Parse(buf);
}

static esp_err_t send_ok(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t send_bad_request(httpd_req_t* req) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":false}");
}

static void delayed_app_send_play(void*) {
    app_send_play();
}

static esp_err_t post_api_play(httpd_req_t* req) {
    ESP_LOGI(TAG, "play");

    cJSON* json = recv_json(req);
    if (!json)
        return send_bad_request(req);

    const cJSON* play_at = cJSON_GetObjectItemCaseSensitive(json, "play_at");

    if (cJSON_IsNumber(play_at)) {
        int64_t play_at_tsf = (int64_t)play_at->valuedouble;
        int64_t current_tsf = esp_wifi_get_tsf_time(WIFI_IF_STA);
        int64_t time_to_execute = play_at_tsf - current_tsf;

        ESP_LOGI(TAG, "current_tsf: %" PRId64, current_tsf);
        ESP_LOGI(TAG, "play_at: %" PRId64, play_at_tsf);
        ESP_LOGI(TAG, "time_to_execute: %" PRId64, time_to_execute);

        delayed_execute(delayed_app_send_play, NULL, time_to_execute);
    } else {
        cJSON_Delete(json);
        return send_bad_request(req);
    }

    cJSON_Delete(json);

    return send_ok(req);
}

static esp_err_t post_api_pause(httpd_req_t* req) {
    ESP_LOGI(TAG, "pause");

    // TODO: app_pause()

    return send_ok(req);
}

static esp_err_t post_api_stop(httpd_req_t* req) {
    ESP_LOGI(TAG, "stop");

    app_send_stop();

    return send_ok(req);
}

static esp_err_t post_api_test(httpd_req_t* req) {
    ESP_LOGI(TAG, "test");

    cJSON* json = recv_json(req);
    if (!json)
        return send_bad_request(req);

    const cJSON* mode_item = cJSON_GetObjectItemCaseSensitive(json, "mode");

    if (!cJSON_IsString(mode_item)) {
        cJSON_Delete(json);
        return send_bad_request(req);
    }

    const char* mode = mode_item->valuestring;
    ESP_LOGI(TAG, "mode: %s", mode);

    if (strcmp(mode, "solid") == 0) {

        const cJSON* color = cJSON_GetObjectItemCaseSensitive(json, "color");
        const cJSON* r = cJSON_GetObjectItemCaseSensitive(color, "r");
        const cJSON* g = cJSON_GetObjectItemCaseSensitive(color, "g");
        const cJSON* b = cJSON_GetObjectItemCaseSensitive(color, "b");

        if (!cJSON_IsNumber(r) || !cJSON_IsNumber(g) || !cJSON_IsNumber(b)) {
            cJSON_Delete(json);
            return send_bad_request(req);
        }

        ESP_LOGI(TAG, "RGB: %d %d %d", r->valueint, g->valueint, b->valueint);
        app_send_test_solid(r->valueint, g->valueint, b->valueint);
    } else if (strcmp(mode, "rainbow") == 0) {
        app_send_test_rainbow();
    }

    cJSON_Delete(json);
    return send_ok(req);
}

static esp_err_t post_api_reset(httpd_req_t* req) {
    ESP_LOGI(TAG, "reset");

    // TODO: app_reset()

    return send_ok(req);
}

static esp_err_t post_api_upload(httpd_req_t* req) {
    ESP_LOGI(TAG, "upload");

    cJSON* json = recv_json(req);
    if (!json)
        return send_bad_request(req);

    const cJSON* url = cJSON_GetObjectItemCaseSensitive(json, "url");

    if (!cJSON_IsString(url)) {
        cJSON_Delete(json);
        return send_bad_request(req);
    }

    ESP_LOGI(TAG, "url: %s", url->valuestring);

    // TODO: app_upload()

    cJSON_Delete(json);
    return send_ok(req);
}

static esp_err_t get_api_status(httpd_req_t* req) {
    int64_t tsf = esp_wifi_get_tsf_time(WIFI_IF_STA);

    char response[64];
    snprintf(response, sizeof(response), "{\"tsf_us\":%" PRId64 "}", tsf);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

esp_err_t http_register_api() {
    http_server_register_post("/api/play", post_api_play, NULL);
    http_server_register_post("/api/pause", post_api_pause, NULL);
    http_server_register_post("/api/stop", post_api_stop, NULL);
    http_server_register_post("/api/test", post_api_test, NULL);
    http_server_register_post("/api/reset", post_api_reset, NULL);
    http_server_register_post("/api/upload", post_api_upload, NULL);

    http_server_register_get("/api/status", get_api_status, NULL);

    return ESP_OK;
}
