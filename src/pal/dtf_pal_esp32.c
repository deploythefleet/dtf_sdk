/*
 * dtf_pal_esp32.c — ESP32 default PAL implementations (weak-linked).
 *
 * Every function here is __attribute__((weak)), so any of the four primary
 * interfaces can be replaced by:
 *   1. A strong (non-weak) definition anywhere in the application, OR
 *   2. A callback pointer set in dtf_config_t before calling dtf_init().
 *
 * This file is the only place in the DTF SDK that may include esp_*.h headers.
 */

#include "dtf_pal.h"

#include <stdlib.h>
#include <stdio.h>

/* ESP-IDF headers */
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* ---------------------------------------------------------------------------
 * Storage — NVS-backed BSN persistence
 * --------------------------------------------------------------------------- */

#define DTF_NVS_NAMESPACE "dtf"
#define DTF_NVS_KEY_BSN   "bsn"

__attribute__((weak))
int dtf_pal_storage_read_bsn(uint32_t *bsn)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(DTF_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return -1;
    err = nvs_get_u32(h, DTF_NVS_KEY_BSN, bsn);
    nvs_close(h);
    return (err == ESP_OK) ? 0 : -1;
}

__attribute__((weak))
int dtf_pal_storage_write_bsn(uint32_t bsn)
{
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

__attribute__((weak))
int dtf_pal_transport_send(const char *url, const char *auth_header,
                           const uint8_t *payload, size_t len)
{
    esp_http_client_config_t cfg = {
        .url              = url,
        .method           = HTTP_METHOD_POST,
        .timeout_ms       = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -1;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_post_field(client, (const char *)payload, (int)len);

    esp_err_t err = esp_http_client_perform(client);
    int result = -1;
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        result = (status == 202) ? 0 : status;
        if (result != 0) {
            fprintf(stderr, "[dtf_pal] transport: HTTP %d\n", status);
        }
    } else {
        fprintf(stderr, "[dtf_pal] transport: esp_err=0x%x\n", (unsigned)err);
    }

    esp_http_client_cleanup(client);
    return result;
}

/* ---------------------------------------------------------------------------
 * Clock — esp_timer monotonic uptime
 * --------------------------------------------------------------------------- */

__attribute__((weak))
uint32_t dtf_pal_clock_uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

/* ---------------------------------------------------------------------------
 * Timer — dedicated FreeRTOS task (avoids timer-task stack limitations)
 * --------------------------------------------------------------------------- */

typedef struct {
    uint32_t  interval_ms;
    void    (*callback)(void *);
    void     *arg;
} dtf_timer_ctx_t;

static void dtf_flush_task(void *arg)
{
    dtf_timer_ctx_t *ctx = (dtf_timer_ctx_t *)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(ctx->interval_ms));
        ctx->callback(ctx->arg);
    }
}

__attribute__((weak))
int dtf_pal_timer_create(uint32_t interval_ms, void (*callback)(void *),
                          void *arg)
{
    dtf_timer_ctx_t *ctx = malloc(sizeof(dtf_timer_ctx_t));
    if (!ctx) return -1;
    ctx->interval_ms = interval_ms;
    ctx->callback    = callback;
    ctx->arg         = arg;

    /* 8 KiB stack — sufficient for HTTP client + JSON payload allocation */
    BaseType_t ret = xTaskCreate(dtf_flush_task, "dtf_flush",
                                 8192, ctx, 5, NULL);
    if (ret != pdPASS) {
        free(ctx);
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Mutex — FreeRTOS semaphore
 * --------------------------------------------------------------------------- */

__attribute__((weak))
void *dtf_pal_mutex_create(void)
{
    return (void *)xSemaphoreCreateMutex();
}

__attribute__((weak))
void dtf_pal_mutex_lock(void *mutex)
{
    xSemaphoreTake((SemaphoreHandle_t)mutex, portMAX_DELAY);
}

__attribute__((weak))
void dtf_pal_mutex_unlock(void *mutex)
{
    xSemaphoreGive((SemaphoreHandle_t)mutex);
}
