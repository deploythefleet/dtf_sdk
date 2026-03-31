/*
 * dtf_pal_esp32.c — ESP32 default PAL implementations (weak-linked).
 *
 * Every function here is __attribute__((weak)), so any of the primary
 * interfaces can be replaced by:
 *   1. A strong (non-weak) definition anywhere in the application, OR
 *   2. A callback pointer set in dtf_config_t before calling dtf_init().
 *
 * This file is the only place in the DTF SDK that may include esp_*.h headers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dtf_pal.h"

/* ESP-IDF headers */
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

/* ---------------------------------------------------------------------------
 * BSN persistence — NVS-backed
 * --------------------------------------------------------------------------- */

#define DTF_NVS_NAMESPACE "dtf"
#define DTF_NVS_KEY_BSN "bsn"

__attribute__((weak)) int dtf_pal_read_bsn(uint32_t* bsn) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(DTF_NVS_NAMESPACE, NVS_READONLY, &h);
  if (err != ESP_OK) return -1;
  err = nvs_get_u32(h, DTF_NVS_KEY_BSN, bsn);
  nvs_close(h);
  return (err == ESP_OK) ? 0 : -1;
}

__attribute__((weak)) int dtf_pal_write_bsn(uint32_t bsn) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(DTF_NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK) return -1;
  err = nvs_set_u32(h, DTF_NVS_KEY_BSN, bsn);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return (err == ESP_OK) ? 0 : -1;
}

/* ---------------------------------------------------------------------------
 * Transport — esp_http_client POST
 * --------------------------------------------------------------------------- */

__attribute__((weak)) int dtf_pal_transport_send(const char* url, const char* auth_header, const uint8_t* payload,
                                                 size_t len) {
  esp_http_client_config_t cfg = {
      .url = url,
      .method = HTTP_METHOD_POST,
      .timeout_ms = 15000,
      .crt_bundle_attach = esp_crt_bundle_attach,
  };

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) return -1;

  esp_http_client_set_header(client, "Content-Type", "application/cbor");
  esp_http_client_set_header(client, "Authorization", auth_header);
  esp_http_client_set_post_field(client, (const char*)payload, (int)len);

  fprintf(stderr, "[dtf_pal] POST %s (%d bytes)\n", url, (int)len);
  esp_err_t err = esp_http_client_perform(client);
  int result = -1;
  if (err == ESP_OK) {
    int status = esp_http_client_get_status_code(client);
    result = (status == 202) ? 0 : status;
    if (result != 0) {
      fprintf(stderr, "[dtf_pal] transport: HTTP %d\n", status);
    }
  }
  else {
    fprintf(stderr, "[dtf_pal] transport: esp_err=0x%x\n", (unsigned)err);
  }

  esp_http_client_cleanup(client);
  return result;
}

/* ---------------------------------------------------------------------------
 * Clock — esp_timer monotonic uptime
 * --------------------------------------------------------------------------- */

__attribute__((weak)) uint32_t dtf_pal_clock_uptime_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000LL); }

/* ---------------------------------------------------------------------------
 * Timer — dedicated FreeRTOS task (avoids timer-task stack limitations)
 * --------------------------------------------------------------------------- */

typedef struct {
  uint32_t interval_ms;
  void (*callback)(void*);
  void* arg;
} dtf_timer_ctx_t;

static void dtf_flush_task(void* arg) {
  dtf_timer_ctx_t* ctx = (dtf_timer_ctx_t*)arg;
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(ctx->interval_ms));
    ctx->callback(ctx->arg);
  }
}

__attribute__((weak)) int dtf_pal_timer_create(uint32_t interval_ms, void (*callback)(void*), void* arg) {
  /* Static context — only one timer is ever created */
  static dtf_timer_ctx_t ctx;
  ctx.interval_ms = interval_ms;
  ctx.callback = callback;
  ctx.arg = arg;

  /* 8 KiB stack — sufficient for HTTP client + CBOR payload */
  BaseType_t ret = xTaskCreate(dtf_flush_task, "dtf_flush", 8192, &ctx, 5, NULL);
  return (ret == pdPASS) ? 0 : -1;
}

/* ---------------------------------------------------------------------------
 * Device identity — derived from hardware when not configured
 * --------------------------------------------------------------------------- */

static char s_default_device_id[13]; /* "aabbccddeeff" + NUL */
static char s_default_hw_variant[16];

const char* dtf_pal_get_device_id(void) {
  if (s_default_device_id[0] == '\0') {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_default_device_id, sizeof(s_default_device_id), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
  }
  return s_default_device_id;
}

const char* dtf_pal_get_hw_variant(void) {
  if (s_default_hw_variant[0] == '\0') {
    esp_chip_info_t info;
    esp_chip_info(&info);
    switch (info.model) {
      case CHIP_ESP32:
        strncpy(s_default_hw_variant, "esp32", sizeof(s_default_hw_variant));
        break;
      case CHIP_ESP32S2:
        strncpy(s_default_hw_variant, "esp32s2", sizeof(s_default_hw_variant));
        break;
      case CHIP_ESP32S3:
        strncpy(s_default_hw_variant, "esp32s3", sizeof(s_default_hw_variant));
        break;
      case CHIP_ESP32C3:
        strncpy(s_default_hw_variant, "esp32c3", sizeof(s_default_hw_variant));
        break;
      case CHIP_ESP32C6:
        strncpy(s_default_hw_variant, "esp32c6", sizeof(s_default_hw_variant));
        break;
      case CHIP_ESP32H2:
        strncpy(s_default_hw_variant, "esp32h2", sizeof(s_default_hw_variant));
        break;
      default:
        snprintf(s_default_hw_variant, sizeof(s_default_hw_variant), "esp32-%d", (int)info.model);
        break;
    }
  }
  return s_default_hw_variant;
}

const char* dtf_pal_get_fw_version(void) {
  const esp_app_desc_t* desc = esp_app_get_description();
  return desc->version;
}

/* ---------------------------------------------------------------------------
 * Mutex — FreeRTOS semaphore
 * --------------------------------------------------------------------------- */

__attribute__((weak)) void* dtf_pal_mutex_create(void) { return (void*)xSemaphoreCreateMutex(); }

__attribute__((weak)) void dtf_pal_mutex_lock(void* mutex) { xSemaphoreTake((SemaphoreHandle_t)mutex, portMAX_DELAY); }

__attribute__((weak)) void dtf_pal_mutex_unlock(void* mutex) { xSemaphoreGive((SemaphoreHandle_t)mutex); }
