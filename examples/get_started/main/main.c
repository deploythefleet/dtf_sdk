#include <stdio.h>
#include <esp_event.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dtf_ota.h"
#include "dtf_obs.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "wifi_connect.h"

#define DTF_PRODUCT_ID ""
#define DTF_API_KEY    ""   /* Set your DTF API key here */

void app_main(void)
{
    /* Initialize NVS (required for OTA state and BSN persistence) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /*
     * --- Minimal observability setup (new user) ---
     *
     * Provide an API key and device ID — the SDK handles the rest.
     * BSN is read from NVS, incremented, and persisted automatically.
     * A background task flushes buffered events to the gateway every
     * CONFIG_DTF_OBS_FLUSH_INTERVAL_MS milliseconds.
     */
    dtf_config_t obs_cfg = DTF_CONFIG_DEFAULT();
    obs_cfg.api_key               = DTF_API_KEY;
    obs_cfg.device_id             = "board-7a3f";
    obs_cfg.fw_version            = dtf_get_active_fw_version();
    obs_cfg.hw_variant            = "esp32-s3";
    obs_cfg.observability_enabled = true;
    dtf_init(&obs_cfg);

    dtf_log(DTF_LOG_INFO, "main", "app started");

    printf("Current firmware version: %s\n", dtf_get_active_fw_version());
    printf("Connecting to WiFi\n");

    connect_to_wifi();

    if (is_wifi_connected()) {
        dtf_log(DTF_LOG_INFO, "wifi", "connected");
        dtf_metric("rssi", -67);

        printf("Checking for updates from Deploy the Fleet\n");
        const dtf_ota_cfg_t cfg = {
            .product_id    = DTF_PRODUCT_ID,
            .reboot_option = DTF_REBOOT_ON_SUCCESS,
        };
        dtf_get_firmware_update(&cfg);
    } else {
        dtf_log(DTF_LOG_WARN, "wifi", "connection failed");
    }
}

/*
 * --- Advanced: custom storage PAL override ---
 *
 * Replace the NVS BSN storage with your own implementation by setting
 * storage_read_bsn / storage_write_bsn in the config struct.
 *
 * static uint32_t my_bsn = 0;
 *
 * static int my_read_bsn(uint32_t *bsn)  { *bsn = my_bsn; return 0; }
 * static int my_write_bsn(uint32_t bsn)  { my_bsn = bsn;  return 0; }
 *
 * dtf_config_t obs_cfg = DTF_CONFIG_DEFAULT();
 * obs_cfg.api_key               = DTF_API_KEY;
 * obs_cfg.device_id             = "board-7a3f";
 * obs_cfg.observability_enabled = true;
 * obs_cfg.storage_read_bsn      = my_read_bsn;
 * obs_cfg.storage_write_bsn     = my_write_bsn;
 * dtf_init(&obs_cfg);
 */
