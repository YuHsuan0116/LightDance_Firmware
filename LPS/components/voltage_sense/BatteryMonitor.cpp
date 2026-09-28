#include "BatteryMonitor.hpp"

#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "esp_log.h"

static const char* TAG = "BATTERY_MONITOR";

static constexpr adc_unit_t ADC_UNIT = ADC_UNIT_1;
static constexpr adc_channel_t ADC_CHANNEL = ADC_CHANNEL_5;
static constexpr adc_atten_t ADC_ATTENUATION = ADC_ATTEN_DB_6;
static constexpr uint32_t ADC_DEFAULT_VREF_MV = 1100U;

BatteryMonitor& BatteryMonitor::getInstance() {
    static BatteryMonitor instance;
    return instance;
}

BatteryMonitor::~BatteryMonitor() {
    deinit();
}

esp_err_t BatteryMonitor::init() {
    if(adcHandle != nullptr) {
        return ESP_OK;
    }

    esp_err_t ret;
    adc_oneshot_unit_init_cfg_t unitConfig = {
        .unit_id = ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    };

    adc_oneshot_chan_cfg_t channelConfig = {
        .atten = ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_DEFAULT
    };

    adc_cali_line_fitting_config_t calibrationConfig = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .default_vref = ADC_DEFAULT_VREF_MV
    };

    ESP_GOTO_ON_ERROR(adc_oneshot_new_unit(&unitConfig, &adcHandle), err, TAG, "ADC1 initialization failed");
    ESP_GOTO_ON_ERROR(adc_oneshot_config_channel(adcHandle, ADC_CHANNEL, &channelConfig), err, TAG, "ADC channel configuration failed");
    ESP_GOTO_ON_ERROR(adc_cali_create_scheme_line_fitting(&calibrationConfig, &caliHandle), err, TAG, "ADC calibration initialization failed");

    ESP_LOGI(TAG,
             "initialized: gpio=%d channel=%d divider=%lu/%lu samples=%lu",
             ADC_GPIO,
             ADC_CHANNEL,
             static_cast<unsigned long>(DIVIDER_TOP_OHMS),
             static_cast<unsigned long>(DIVIDER_BOTTOM_OHMS),
             static_cast<unsigned long>(SAMPLE_COUNT));
    return ESP_OK;

err:
    deinit();
    return ret;
}

esp_err_t BatteryMonitor::deinit() {
    esp_err_t ret = ESP_OK;

    if(caliHandle != nullptr) {
        ret = adc_cali_delete_scheme_line_fitting(caliHandle);
        caliHandle = nullptr;
    }

    if(adcHandle != nullptr) {
        const esp_err_t adcRet = adc_oneshot_del_unit(adcHandle);
        if(ret == ESP_OK) {
            ret = adcRet;
        }
        adcHandle = nullptr;
    }

    return ret;
}

esp_err_t BatteryMonitor::readMv(uint32_t& batteryMv) {
    ESP_RETURN_ON_FALSE(adcHandle != nullptr && caliHandle != nullptr, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    uint32_t adcMvSum = 0;
    for(uint32_t sample = 0; sample < SAMPLE_COUNT; ++sample) {
        int adcMv = 0;
        ESP_RETURN_ON_ERROR(adc_oneshot_get_calibrated_result(adcHandle, caliHandle, ADC_CHANNEL, &adcMv), TAG, "ADC conversion failed");
        adcMvSum += static_cast<uint32_t>(adcMv);
    }

    const uint32_t adcMvAverage = adcMvSum / SAMPLE_COUNT;
    batteryMv = (adcMvAverage * (DIVIDER_TOP_OHMS + DIVIDER_BOTTOM_OHMS) + (DIVIDER_BOTTOM_OHMS / 2U)) / DIVIDER_BOTTOM_OHMS;
    return ESP_OK;
}
