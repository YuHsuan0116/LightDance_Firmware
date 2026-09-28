idf_component_register(
    SRCS "BatteryMonitor.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_adc esp_driver_gpio
)
