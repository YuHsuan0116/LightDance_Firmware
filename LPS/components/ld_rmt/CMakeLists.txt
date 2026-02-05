idf_component_register(
    SRCS   "src/ws2812b_tx.cpp" "src/encoder/ws2812b_encoder.c" "src/ws2812b_tx.c"

    INCLUDE_DIRS "inc"

    REQUIRES driver esp_timer freertos log common
)

