idf_component_register(SRCS  "main.cpp" "console_new.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES  esp_timer driver LedController Player SD fatfs console
                    )