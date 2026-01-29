idf_component_register(SRCS "sd_logger.c" "frame_config.c" "control_reader.c" "frame_reader.c" "readframe.c" 
                    INCLUDE_DIRS "."
                    REQUIRES Player fatfs common)
