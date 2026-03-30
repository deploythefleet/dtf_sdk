#ifndef DTF_H
#define DTF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Compile-time defaults (overridden by Kconfig when building with ESP-IDF)
 * ------------------------------------------------------------------------- */

#ifndef CONFIG_DTF_OBS_BUFFER_SIZE
#define CONFIG_DTF_OBS_BUFFER_SIZE 32
#endif

#ifndef CONFIG_DTF_OBS_FLUSH_INTERVAL_MS
#define CONFIG_DTF_OBS_FLUSH_INTERVAL_MS 30000
#endif

#ifndef CONFIG_DTF_OBS_GATEWAY_URL
#define CONFIG_DTF_OBS_GATEWAY_URL "https://ingest.deploythefleet.io/v1/ingest"
#endif

/* -------------------------------------------------------------------------
 * Log level constants
 * ------------------------------------------------------------------------- */

typedef enum {
    DTF_LOG_DEBUG = 0,
    DTF_LOG_INFO  = 1,
    DTF_LOG_WARN  = 2,
    DTF_LOG_ERROR = 3,
} dtf_log_level_t;

/* -------------------------------------------------------------------------
 * SDK configuration struct
 * Minimum required fields when observability_enabled = true:
 *   api_key, device_id
 * All PAL callback overrides are optional (NULL = use weak-linked default).
 * ------------------------------------------------------------------------- */

typedef struct {
    /* Required */
    const char *api_key;
    const char *device_id;

    /* Optional device metadata (included in every ingest payload) */
    const char *fw_version;
    const char *hw_variant;

    /* Observability enable flag. When false, dtf_init() is a no-op. */
    bool observability_enabled;

    /* Optional PAL overrides — set to NULL to use ESP32 weak-linked defaults */
    int      (*storage_read_bsn)(uint32_t *bsn);
    int      (*storage_write_bsn)(uint32_t bsn);
    int      (*transport_send)(const char *url, const char *auth_header,
                               const uint8_t *payload, size_t len);
    uint32_t (*clock_uptime_ms)(void);
    int      (*timer_create)(uint32_t interval_ms,
                             void (*callback)(void *), void *arg);

    /* Optional settings — 0 / NULL falls back to Kconfig / compile defaults */
    const char *gateway_url;
    uint32_t    buffer_size;        /* max events in ring buffer */
    uint32_t    flush_interval_ms;  /* periodic flush interval   */
} dtf_config_t;

/* Zero-initialised config with sane compile-time defaults */
#define DTF_CONFIG_DEFAULT() {              \
    .api_key              = NULL,           \
    .device_id            = NULL,           \
    .fw_version           = NULL,           \
    .hw_variant           = NULL,           \
    .observability_enabled = false,         \
    .storage_read_bsn     = NULL,           \
    .storage_write_bsn    = NULL,           \
    .transport_send       = NULL,           \
    .clock_uptime_ms      = NULL,           \
    .timer_create         = NULL,           \
    .gateway_url          = NULL,           \
    .buffer_size          = 0,              \
    .flush_interval_ms    = 0,              \
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * @brief Initialise the DTF SDK.
 *
 * When config->observability_enabled is false (or config is NULL), this is a
 * no-op — zero tasks, zero memory, zero network calls. Existing OTA users
 * are completely unaffected.
 *
 * When observability_enabled is true:
 *  1. Reads bsn from storage, increments, writes back.
 *  2. Creates the periodic flush timer / background task.
 *
 * @param config  Pointer to a dtf_config_t. Use DTF_CONFIG_DEFAULT() as base.
 * @return 0 on success, negative on error.
 */
int dtf_init(const dtf_config_t *config);

/**
 * @brief Queue a log event.
 *
 * Thread-safe. bsn and up are attached automatically.
 *
 * @param level    DTF_LOG_DEBUG / INFO / WARN / ERROR
 * @param module   Optional module/tag string (NULL for untagged)
 * @param message  Log message string
 * @return 0 on success, negative if SDK not initialised or observability off.
 */
int dtf_log(dtf_log_level_t level, const char *module, const char *message);

/**
 * @brief Queue a gauge metric event.
 *
 * Thread-safe. bsn and up are attached automatically.
 *
 * @param name   Metric name (e.g. "hf", "rssi")
 * @param value  Metric value
 * @return 0 on success, negative if SDK not initialised or observability off.
 */
int dtf_metric(const char *name, int32_t value);

#ifdef __cplusplus
}
#endif

#endif /* DTF_H */
