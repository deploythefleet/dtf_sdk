#include "../../include/c_drivers/dtf_ota.h"
#include "esp_ota_ops.h"
#include "esp_tls.h"
#include "spi_flash_mmap.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_mac.h"

const char* TAG = "ota_provider";
static char _update_url[256];
esp_http_client_handle_t _client;

static const char *get_device_id()
{
    static char device_id[13];
    uint8_t mac[6];
    esp_efuse_mac_get_default((uint8_t*)&mac);
    snprintf(device_id, sizeof(device_id), "%02x%02x%02x%02x%02x%02x", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    return device_id;
}

// Unused for now but will be used in the future for more advanced OTA features
esp_err_t _ota_http_client_init_cb(esp_http_client_handle_t client)
{
    // return esp_http_client_set_header(client, "Authorization", "API key or JWT");
    _client = client;
    return ESP_OK;
}

esp_err_t _ota_http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_DISCONNECTED:
        case HTTP_EVENT_ERROR:
        {
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGD(TAG, "Last esp error code: 0x%x\n", err);
                ESP_LOGD(TAG, "Last mbedtls failure: 0x%x\n", mbedtls_err);
                if (err == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED)
                {
                    ESP_LOGE(TAG, "Cert validation failed\n");
                }
            }
            break;
        }
        default:
            break;
    }
    return ESP_OK;
}

DTF_OtaResponse dtf_get_firmware_update(const dtf_ota_cfg_t *cfg)
{
    esp_http_client_config_t config = {};

    snprintf(_update_url, sizeof(_update_url), 
        "https://ota.deploythefleet.io/%s?v=%s&did=%s", 
        cfg->product_id, 
        dtf_get_active_fw_version(), 
        get_device_id()
    );

    config.url = _update_url;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = CONFIG_DTF_OTA_TIMEOUT_MS;
    config.event_handler = _ota_http_event_handler;
    config.buffer_size = CONFIG_DTF_OTA_HTTP_RX_BUFFER_SIZE;
    config.buffer_size_tx = CONFIG_DTF_OTA_HTTP_TX_BUFFER_SIZE;
    config.keep_alive_enable = true;

    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &config;
    ota_config.http_client_init_cb = _ota_http_client_init_cb;

    esp_err_t ret = esp_https_ota(&ota_config);
    
    switch(ret)
    {
        case ESP_OK:
            if(cfg->reboot_option == DTF_REBOOT_ON_SUCCESS)
            {
                ESP_LOGI(TAG, "Firmware update successful. Rebooting...");
                esp_restart();
            }
            return DTFOTA_NewFirmwareFlashed;
            break;
        case ESP_ERR_OTA_VALIDATE_FAILED:
            return DTFOTA_InvalidFirmwareImage;
            break;
        case ESP_ERR_NO_MEM:
            return DTFOTA_NotEnoughMemory;
            break;
        case ESP_ERR_FLASH_OP_TIMEOUT:
        case ESP_ERR_FLASH_OP_FAIL:
            return DTFOTA_FirmwareWriteFailed;
            break;
        case ESP_FAIL:
            ESP_LOGW(TAG, "OTA Error: %d", ret);
            int status_code = esp_http_client_get_status_code(_client);
            ESP_LOGD(TAG, "HTTP OTA Status: %d", status_code);
            if(status_code == 304)
            {
                ESP_LOGI(TAG, "No updates available");
                return DTFOTA_NoUpdatesAvailable;
            }
            return DTFOTA_UnknownError;
            break;
        default:
            return DTFOTA_UnknownError;
            break;
    }
}

const char* dtf_get_active_fw_version()
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    return app_desc->version;
}