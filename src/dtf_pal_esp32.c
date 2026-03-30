/*
 * dtf_pal_esp32.c — ESP32 default PAL implementations (weak-linked).
 *
 * All functions are declared __attribute__((weak)) so that:
 *   (a) users can override any individual function with a strong-linked impl, and
 *   (b) the entire file compiles away to nothing when CONFIG_DTF_OBSERVABILITY
 *       is disabled (no references from dtf_obs.c ⇒ linker dead-strips).
 *
 * Platform-specific headers are confined to this file. The core (dtf_obs.c)
 * has zero esp_*.h includes.
 */

#ifdef CONFIG_DTF_OBSERVABILITY

#include "dtf_pal.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "dtf_pal";

/* -------------------------------------------------------------------------
 * Storage — NVS namespace "dtf", key "bsn"
 * ------------------------------------------------------------------------- */

__attribute__((weak))
int dtf_pal_storage_read_bsn(uint32_t *bsn)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("dtf", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return -1;
    }

    err = nvs_get_u32(handle, "bsn", bsn);
    nvs_close(handle);

    return (err == ESP_OK) ? 0 : -1;
}

__attribute__((weak))
int dtf_pal_storage_write_bsn(uint32_t bsn)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("dtf", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return -1;
    }

    err = nvs_set_u32(handle, "bsn", bsn);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    return (err == ESP_OK) ? 0 : -1;
}

/* -------------------------------------------------------------------------
 * Transport — esp_http_client POST
 * ------------------------------------------------------------------------- */

__attribute__((weak))
int dtf_pal_transport_send(const char *url, const char *auth_header,
                           const uint8_t *payload, size_t len)
{
    esp_http_client_config_t cfg = {
        .url               = url,
        .method            = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "transport: failed to init http client");
        return -1;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_post_field(client, (const char *)payload, (int)len);

    esp_err_t err = esp_http_client_perform(client);
    int status = -1;
    int ret = -1;

    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
        if (status == 202) {
            ret = 0;
        } else if (status == 400) {
            ESP_LOGE(TAG, "transport: ingest rejected payload (400)");
        } else if (status == 401) {
            ESP_LOGE(TAG, "transport: authentication failed (401)");
        } else if (status == 429) {
            ESP_LOGW(TAG, "transport: rate limited (429)");
        } else {
            ESP_LOGW(TAG, "transport: unexpected status %d", status);
        }
    } else {
        ESP_LOGE(TAG, "transport: http error 0x%x", err);
    }

    esp_http_client_cleanup(client);
    return ret;
}

/* -------------------------------------------------------------------------
 * Clock — esp_timer monotonic uptime
 * ------------------------------------------------------------------------- */

__attribute__((weak))
uint32_t dtf_pal_clock_uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* -------------------------------------------------------------------------
 * Timer — FreeRTOS task (safe for blocking HTTP calls in callback)
 * ------------------------------------------------------------------------- */

typedef struct {
    uint32_t    interval_ms;
    void      (*callback)(void *);
    void       *arg;
} dtf_timer_task_params_t;

static void dtf_timer_task(void *pv_param)
{
    dtf_timer_task_params_t *p = (dtf_timer_task_params_t *)pv_param;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(p->interval_ms));
        p->callback(p->arg);
    }
}

__attribute__((weak))
int dtf_pal_timer_create(uint32_t interval_ms,
                         void (*callback)(void *), void *arg)
{
    dtf_timer_task_params_t *params =
        (dtf_timer_task_params_t *)malloc(sizeof(dtf_timer_task_params_t));
    if (!params) {
        return -1;
    }

    params->interval_ms = interval_ms;
    params->callback    = callback;
    params->arg         = arg;

    /* params is intentionally not freed — the task runs for the lifetime
     * of the SDK and there is no timer-destroy API in M1. */
    BaseType_t ret = xTaskCreate(dtf_timer_task, "dtf_flush",
                                 4096, params, 5, NULL);
    if (ret != pdPASS) {
        free(params);
        return -1;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Mutex — FreeRTOS semaphore
 * ------------------------------------------------------------------------- */

__attribute__((weak))
void *dtf_pal_mutex_create(void)
{
    return (void *)xSemaphoreCreateMutex();
}

__attribute__((weak))
void dtf_pal_mutex_lock(void *mutex)
{
    if (mutex) {
        xSemaphoreTake((SemaphoreHandle_t)mutex, portMAX_DELAY);
    }
}

__attribute__((weak))
void dtf_pal_mutex_unlock(void *mutex)
{
    if (mutex) {
        xSemaphoreGive((SemaphoreHandle_t)mutex);
    }
}

#endif /* CONFIG_DTF_OBSERVABILITY */
