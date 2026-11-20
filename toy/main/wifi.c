#include "wifi.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char* TAG = "wifi";

#define CONNECTED_BIT BIT0
#define FAILED_BIT BIT1
#define MAX_RETRY 5
#define CONNECT_TIMEOUT_MS 30000

static EventGroupHandle_t s_event_group = NULL;

static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;

static int s_retry_count = 0;
static bool s_initialized = false;
static bool s_started = false;

static void wifi_try_connect(void) {
    esp_err_t err = esp_wifi_connect();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(err));
        xEventGroupSetBits(s_event_group, FAILED_BIT);
    }
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                          void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        wifi_try_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_event_group, CONNECTED_BIT);

        if (s_retry_count < MAX_RETRY) {
            s_retry_count++;
            ESP_LOGW(TAG, "Retry connecting (%d/%d)", s_retry_count, MAX_RETRY);
            wifi_try_connect();
        } else {
            ESP_LOGE(TAG, "Maximum retries reached");
            xEventGroupSetBits(s_event_group, FAILED_BIT);
        }

        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_count = 0;

        xEventGroupClearBits(s_event_group, FAILED_BIT);
        xEventGroupSetBits(s_event_group, CONNECTED_BIT);

        ESP_LOGI(TAG, "WiFi connected");
        return;
    }
}

static esp_err_t wifi_init(void) {
    ESP_RETURN_ON_FALSE(!s_initialized, ESP_ERR_INVALID_STATE, TAG, "WiFi already initialized");

    s_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_event_group != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create event group");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to initialize netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Failed to create event loop");
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta() != NULL, ESP_FAIL, TAG,
                        "Failed to create WiFi station");

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), TAG, "Failed to initialize WiFi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Failed to set WiFi storage");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                            event_handler, NULL,
                                                            &s_wifi_event_instance),
                        TAG, "Failed to register WiFi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                            event_handler, NULL,
                                                            &s_ip_event_instance),
                        TAG, "Failed to register IP event handler");

    s_initialized = true;

    return ESP_OK;
}

esp_err_t wifi_connect(const char* ssid, const char* pwd) {
    wifi_init();

    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "WiFi not initialized");
    ESP_RETURN_ON_FALSE(!s_started, ESP_ERR_INVALID_STATE, TAG, "WiFi already started");
    ESP_RETURN_ON_FALSE(ssid != NULL && pwd != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Invalid SSID or password");

    wifi_config_t config = {0};

    size_t ssid_len = strlen(ssid);
    size_t pwd_len = strlen(pwd);

    ESP_RETURN_ON_FALSE(ssid_len > 0 && ssid_len <= sizeof(config.sta.ssid), ESP_ERR_INVALID_ARG,
                        TAG, "Invalid SSID length");
    ESP_RETURN_ON_FALSE(pwd_len <= sizeof(config.sta.password), ESP_ERR_INVALID_ARG, TAG,
                        "Invalid password length");

    memcpy(config.sta.ssid, ssid, ssid_len);
    memcpy(config.sta.password, pwd, pwd_len);

    config.sta.scan_method = WIFI_FAST_SCAN;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set WiFi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG,
                        "Failed to set WiFi config");

    s_retry_count = 0;
    xEventGroupClearBits(s_event_group, CONNECTED_BIT | FAILED_BIT);

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start WiFi");

    s_started = true;

    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "Failed to disable power saving");

    EventBits_t bits = xEventGroupWaitBits(s_event_group, CONNECTED_BIT | FAILED_BIT, pdFALSE,
                                           pdFALSE, pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));

    if (bits & CONNECTED_BIT) {
        return ESP_OK;
    }

    if (bits & FAILED_BIT) {
        return ESP_FAIL;
    }

    return ESP_ERR_TIMEOUT;
}
