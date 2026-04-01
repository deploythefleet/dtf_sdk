set(srcs
    "src/serial_runtime_profiler.cpp"
    "src/c_drivers/dtf_logging.c"
    "src/c_drivers/dtf_ota.c"
)

set(priv_requires
    pthread
    app_update
    esp-tls
    spi_flash
    esp_http_client
    esp_https_ota
)

idf_component_register(
    SRCS            ${srcs}
    PRIV_REQUIRES   ${priv_requires}
    INCLUDE_DIRS
        "."
        "./include"
        "./include/c_drivers"
)

if(CONFIG_DTF_OBSERVABILITY)
    target_sources(${COMPONENT_LIB} PRIVATE
        "src/dtf_obs.c"
        "src/pal/dtf_pal_esp32.c"
    )
    target_link_libraries(${COMPONENT_LIB} PRIVATE
        idf::nvs_flash
        idf::esp_timer
    )
endif()
