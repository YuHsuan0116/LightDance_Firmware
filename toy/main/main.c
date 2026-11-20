#include "app.h"
#include "http_handler.h"
#include "http_server.h"
#include "wifi.h"

#include "nvs_flash.h"

#define SSID "lightdance"
#define PWD "ntueelightdance"

void app_main(void) {
    nvs_flash_init();

    wifi_connect(SSID, PWD);

    app_init();

    http_server_run();
    http_register_api();
}
