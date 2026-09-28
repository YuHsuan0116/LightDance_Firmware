idf_component_register(SRCS  "main.cpp" 
                    INCLUDE_DIRS "."
                    REQUIRES  esp_timer driver LedController Player console PT_Reader Messenger SD FileDownloader ld_storage voltage_sense
                    )

spiffs_create_partition_image(
    storage
    ../spiffs_data
    FLASH_IN_PROJECT
)
