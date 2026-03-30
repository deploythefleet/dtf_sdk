idf_component_register(SRCS 
                        "src/serial_runtime_profiler.cpp"
                        "src/c_drivers/dtf_logging.c"
                        "src/c_drivers/dtf_ota.c"
                        "src/dtf_obs.c"
                        "src/dtf_pal_esp32.c"
                       PRIV_REQUIRES pthread app_update esp-tls spi_flash esp_http_client esp_https_ota nvs_flash esp_timer
                       INCLUDE_DIRS 
                        "."
                        "./include"
                        "./include/c_drivers"
                       )