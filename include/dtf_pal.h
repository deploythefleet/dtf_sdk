#ifndef DTF_PAL_H
#define DTF_PAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup dtf_pal Platform Abstraction Layer
 *
 * Each function has a weak-linked ESP32 default implementation in
 * src/pal/dtf_pal_esp32.c.  To replace a specific interface, either:
 *   1. Provide a strong (non-weak) definition in your application, OR
 *   2. Set the corresponding callback in dtf_config_t before dtf_init().
 *
 * All primary interfaces are independently replaceable.
 * @{
 */

/* -------------------------------------------------------------------------
 * BSN persistence — boot sequence number across reboots
 * -------------------------------------------------------------------------*/

/**
 * @brief Read the boot sequence number from persistent storage.
 *
 * ESP32 default: NVS namespace "dtf", key "bsn".
 *
 * @param[out] bsn  Set to the stored value on success.
 * @return 0 on success, non-zero if the key is absent or the read failed.
 */
int dtf_pal_read_bsn(uint32_t *bsn);

/**
 * @brief Write the boot sequence number to persistent storage.
 *
 * ESP32 default: NVS namespace "dtf", key "bsn".
 *
 * @param bsn  Value to persist.
 * @return 0 on success, non-zero on failure.
 */
int dtf_pal_write_bsn(uint32_t bsn);

/* -------------------------------------------------------------------------
 * Transport — ingest gateway delivery
 * -------------------------------------------------------------------------*/

/**
 * @brief POST a CBOR payload to the given URL.
 *
 * ESP32 default: esp_http_client with Content-Type: application/cbor.
 *
 * @param url          Ingest endpoint URL.
 * @param auth_header  Full Authorization header value ("Bearer dtf_k_...").
 * @param payload      CBOR payload bytes.
 * @param len          Payload length in bytes.
 * @return 0 on HTTP 202, non-zero on network or server error.
 */
int dtf_pal_transport_send(const char *url, const char *auth_header,
                           const uint8_t *payload, size_t len);

/* -------------------------------------------------------------------------
 * Clock — monotonic uptime
 * -------------------------------------------------------------------------*/

/**
 * @brief Return milliseconds elapsed since device boot.
 *
 * Must be monotonically increasing for the duration of a single boot.
 * ESP32 default: esp_timer_get_time() / 1000.
 *
 * @return Uptime in milliseconds.
 */
uint32_t dtf_pal_clock_uptime_ms(void);

/* -------------------------------------------------------------------------
 * Timer — periodic pipeline trigger
 * -------------------------------------------------------------------------*/

/**
 * @brief Create and start a periodic timer that calls @p callback every
 *        @p interval_ms milliseconds.
 *
 * The callback is invoked from a context with sufficient stack for HTTP
 * operations (>= 8 KiB on the ESP32 default).  The timer runs for the
 * lifetime of the application.
 *
 * ESP32 default: dedicated FreeRTOS task with 8 KiB stack.
 *
 * @param interval_ms  Period in milliseconds.
 * @param callback     Function to invoke on each tick.
 * @param arg          Opaque argument forwarded to @p callback.
 * @return 0 on success, non-zero on failure.
 */
int dtf_pal_timer_create(uint32_t interval_ms, void (*callback)(void *),
                          void *arg);

/* -------------------------------------------------------------------------
 * Mutex — internal thread-safety (not exposed in dtf_config_t)
 * -------------------------------------------------------------------------*/

/**
 * @brief Allocate and initialise a mutual-exclusion lock.
 * @return Opaque handle, or NULL on allocation failure.
 */
void *dtf_pal_mutex_create(void);

/**
 * @brief Acquire the lock (blocks indefinitely until available).
 * @param mutex  Handle returned by dtf_pal_mutex_create().
 */
void dtf_pal_mutex_lock(void *mutex);

/**
 * @brief Release the lock.
 * @param mutex  Handle returned by dtf_pal_mutex_create().
 */
void dtf_pal_mutex_unlock(void *mutex);

/* -------------------------------------------------------------------------
 * Device identity defaults — platform-derived
 * -------------------------------------------------------------------------*/

/**
 * @brief Return a default device ID derived from the WiFi STA MAC address.
 *
 * Returns a 12-character lowercase hex string (e.g., "aabbccddeeff").
 * The result is cached after the first call.
 */
const char *dtf_pal_get_device_id(void);

/**
 * @brief Return a default hardware variant derived from esp_chip_info().
 *
 * Returns a string like "esp32", "esp32s3", "esp32c3", etc.
 * The result is cached after the first call.
 */
const char *dtf_pal_get_hw_variant(void);

/**
 * @brief Return the firmware version string.
 *
 * ESP32 default: esp_app_get_description()->version (set by PROJECT_VER
 * in the project's root CMakeLists.txt).
 */
const char *dtf_pal_get_fw_version(void);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* DTF_PAL_H */
