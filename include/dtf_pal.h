#ifndef DTF_PAL_H
#define DTF_PAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Platform Abstraction Layer (PAL) interface declarations
 *
 * Each function has a weak-linked ESP32 default in dtf_pal_esp32.c.
 * Override any function by:
 *   (a) providing a strong-linked implementation in your own source, OR
 *   (b) setting the corresponding callback in dtf_config_t before dtf_init().
 * ------------------------------------------------------------------------- */

/* --- Storage --- */

/**
 * @brief Read the boot sequence number from persistent storage.
 * ESP32 default: NVS namespace "dtf", key "bsn".
 * @param bsn  Output pointer.
 * @return 0 on success, negative on error (first boot / key not found).
 */
int dtf_pal_storage_read_bsn(uint32_t *bsn);

/**
 * @brief Write the boot sequence number to persistent storage.
 * ESP32 default: NVS namespace "dtf", key "bsn".
 * @param bsn  Value to persist.
 * @return 0 on success, negative on error.
 */
int dtf_pal_storage_write_bsn(uint32_t bsn);

/* --- Transport --- */

/**
 * @brief POST a JSON payload to the DTF ingest gateway.
 * ESP32 default: esp_http_client POST, Content-Type: application/json.
 * @param url          Full endpoint URL.
 * @param auth_header  Value for the Authorization header (e.g. "Bearer dtf_k_…").
 * @param payload      JSON body bytes.
 * @param len          Payload length in bytes.
 * @return 0 on HTTP 202, negative on any error or non-202 response.
 */
int dtf_pal_transport_send(const char *url, const char *auth_header,
                           const uint8_t *payload, size_t len);

/* --- Clock --- */

/**
 * @brief Return monotonic uptime in milliseconds since boot.
 * ESP32 default: esp_timer_get_time() / 1000.
 * @return Uptime in ms.
 */
uint32_t dtf_pal_clock_uptime_ms(void);

/* --- Timer --- */

/**
 * @brief Create a periodic background task that calls callback every interval_ms.
 * ESP32 default: FreeRTOS task wrapping vTaskDelay.
 * @param interval_ms  Period in milliseconds.
 * @param callback     Function to call on each tick.
 * @param arg          Opaque argument passed to callback.
 * @return 0 on success, negative on error.
 */
int dtf_pal_timer_create(uint32_t interval_ms,
                         void (*callback)(void *), void *arg);

/* --- Mutex --- */

/**
 * @brief Create a mutex.
 * ESP32 default: FreeRTOS xSemaphoreCreateMutex().
 * @return Opaque handle, or NULL on failure.
 */
void *dtf_pal_mutex_create(void);

/**
 * @brief Acquire the mutex (blocking).
 * @param mutex  Handle returned by dtf_pal_mutex_create().
 */
void dtf_pal_mutex_lock(void *mutex);

/**
 * @brief Release the mutex.
 * @param mutex  Handle returned by dtf_pal_mutex_create().
 */
void dtf_pal_mutex_unlock(void *mutex);

#ifdef __cplusplus
}
#endif

#endif /* DTF_PAL_H */
