idf_component_register(SRCS 
                        "src/serial_runtime_profiler.cpp"
                        "src/c_drivers/dtf_logging.c"
                        "src/c_drivers/dtf_ota.c"
                       PRIV_REQUIRES pthread app_update esp-tls spi_flash esp_http_client esp_https_ota
                       INCLUDE_DIRS 
                        "."
                        "./include"
                        "./include/c_drivers"
                       )