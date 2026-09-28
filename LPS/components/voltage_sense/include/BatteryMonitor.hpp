#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

class BatteryMonitor {
public:
    static constexpr gpio_num_t ADC_GPIO = GPIO_NUM_33;
    static constexpr uint32_t DIVIDER_TOP_OHMS = 910000U;
    static constexpr uint32_t DIVIDER_BOTTOM_OHMS = 100000U;
    static constexpr uint32_t SAMPLE_COUNT = 32U;

    static BatteryMonitor& getInstance();

    BatteryMonitor(const BatteryMonitor&) = delete;
    BatteryMonitor& operator=(const BatteryMonitor&) = delete;

    esp_err_t init();
    esp_err_t deinit();
    esp_err_t readMv(uint32_t& batteryMv);

private:
    BatteryMonitor() = default;
    ~BatteryMonitor();

    adc_oneshot_unit_handle_t adcHandle = nullptr;
    adc_cali_handle_t caliHandle = nullptr;
};
