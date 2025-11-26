#include "dtf_ota.h"
#include "esp_ota_ops.h"
#include "esp_tls.h"
#include "spi_flash_mmap.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_mac.h"

#ifdef CORE_DEBUG_LEVEL
#define LOG_LOCAL_LEVEL CORE_DEBUG_LEVEL
#endif
#include "esp_log.h"

const char *TAG = "dtf_ota";
static esp_http_client_handle_t _client;
static esp_err_t _last_http_err = ESP_OK;

static const char dtf_primary_url[] = "https://ota2.deploythefleet.com/update";
static const char dtf_failover_url[] = "https://ota4.deploythefleet.com/update";

static const char deploy_the_fleet_cert[] =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "Hg==\n"
    "-----END CERTIFICATE-----\n";

static const char *get_device_id()
{
    static char device_id[13];
    uint8_t mac[6];
    esp_efuse_mac_get_default((uint8_t *)&mac);
    snprintf(device_id, sizeof(device_id), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return device_id;
}

esp_err_t _ota_http_client_init_cb(esp_http_client_handle_t client)
{
    dtf_ota_cfg_t *cfg;
    esp_http_client_get_user_data(client, (void **)&cfg);

    esp_err_t err = esp_http_client_set_header(client, "x-dtf-did", get_device_id());
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set device ID header");
    }
    err = esp_http_client_set_header(client, "x-dtf-p", cfg->product_id);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set product ID header");
    }
    if (cfg->custom_version && strlen(cfg->custom_version) > 0)
    {
        err = esp_http_client_set_header(client, "x-dtf-v", cfg->custom_version);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set custom version header");
        }
    }
    else
    {
        err = esp_http_client_set_header(client, "x-dtf-v", dtf_get_active_fw_version());
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set firmware version header");
        }
    }

    _client = client;
    return err;
}

esp_err_t _ota_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_DISCONNECTED:
    case HTTP_EVENT_ERROR:
    {
        int mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
        if (err != 0)
        {
            ESP_LOGD(TAG, "Last esp error code: 0x%x", err);
            ESP_LOGD(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            _last_http_err = err;
            if (err == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED)
            {
                ESP_LOGE(TAG, "Cert validation failed");
            }
            if (err == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME)
            {
                ESP_LOGE(TAG, "Failed to resolve hostname");
            }
            if (err == ESP_ERR_ESP_TLS_CANNOT_CREATE_SOCKET)
            {
                ESP_LOGE(TAG, "Failed to create socket");
            }
            if (err == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST)
            {
                ESP_LOGE(TAG, "Failed to connect to host");
            }
            if (err == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT)
            {
                ESP_LOGE(TAG, "TLS connection timeout");
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
// Allows logging to work with projects built using the Arduino IDE
#ifdef CORE_DEBUG_LEVEL
    esp_log_level_set(TAG, CORE_DEBUG_LEVEL);
#endif

    esp_http_client_config_t config = {};

    config.url = dtf_primary_url;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = CONFIG_DTF_OTA_TIMEOUT_MS;
    config.event_handler = _ota_http_event_handler;
    config.buffer_size = CONFIG_DTF_OTA_HTTP_RX_BUFFER_SIZE;
    config.buffer_size_tx = CONFIG_DTF_OTA_HTTP_TX_BUFFER_SIZE;
    config.keep_alive_enable = true;
    config.user_data = (void *)cfg;

    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &config;
    ota_config.http_client_init_cb = _ota_http_client_init_cb;

    esp_err_t ret = esp_https_ota(&ota_config);

    // If there is a cert problem, try the failover
    if (ret != ESP_OK && (_last_http_err == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED 
        || _last_http_err == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME 
        || _last_http_err == ESP_ERR_ESP_TLS_CANNOT_CREATE_SOCKET 
        || _last_http_err == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST 
        || _last_http_err == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT))
    {
        _last_http_err = ESP_OK;
        ESP_LOGD(TAG, "Trying failover endpoint...");
        config.url = dtf_failover_url;
        config.crt_bundle_attach = NULL; // Disable cert bundle for failover
        config.cert_pem = deploy_the_fleet_cert;
        config.cert_len = sizeof(deploy_the_fleet_cert);
        ret = esp_https_ota(&ota_config);
    }

    switch (ret)
    {
    case ESP_OK:
        ESP_LOGI(TAG, "Firmware update successful.");
        if (cfg->reboot_option == DTF_REBOOT_ON_SUCCESS)
        {
            ESP_LOGI(TAG, "Rebooting...");
            esp_restart();
        }
        return DTFOTA_NewFirmwareFlashed;
        break;
    case ESP_ERR_OTA_VALIDATE_FAILED:
        ESP_LOGE(TAG, "Firmware image validation failed.");
        return DTFOTA_InvalidFirmwareImage;
        break;
    case ESP_ERR_NO_MEM:
        ESP_LOGE(TAG, "Not enough memory to perform OTA update.");
        return DTFOTA_NotEnoughMemory;
        break;
    case ESP_ERR_INVALID_VERSION:
        ESP_LOGE(TAG, "Invalid firmware version.");
        return DTFOTA_InvalidFirmwareImage;
        break;
    case ESP_ERR_FLASH_OP_TIMEOUT:
    case ESP_ERR_FLASH_OP_FAIL:
        ESP_LOGE(TAG, "Firmware write failed.");
        return DTFOTA_FirmwareWriteFailed;
        break;
    case ESP_ERR_HTTP_NOT_MODIFIED:
        // Newer versions of esp_https_ota return this error code for 304 Not Modified
        // instead of ESP_FAIL in older versions
        ESP_LOGI(TAG, "No updates available");
        return DTFOTA_NoUpdatesAvailable;
        break;
    case ESP_FAIL:
        int status_code = esp_http_client_get_status_code(_client);
        ESP_LOGD(TAG, "HTTP OTA Status: %d", status_code);
        if (status_code == 304 || status_code == 204)
        {
            ESP_LOGI(TAG, "No updates available");
            return DTFOTA_NoUpdatesAvailable;
        }
        if (_last_http_err == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED)
        {
            ESP_LOGE(TAG, "Cert validation failed");
            return DTFOTA_CertValidationFailed;
        }
        else
        {
            ESP_LOGW(TAG, "OTA Error: %d", ret);
        }
        return DTFOTA_UnknownError;
        break;
    default:
        ESP_LOGE(TAG, "Unknown OTA error: %d", ret);
        return DTFOTA_UnknownError;
        break;
    }
}

const char *dtf_get_active_fw_version()
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    return app_desc->version;
}