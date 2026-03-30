#ifndef DTF_OBS_H
#define DTF_OBS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log level for dtf_log() events shipped to the ingest gateway.
 */
typedef enum {
    DTF_LOG_DEBUG = 0,
    DTF_LOG_INFO,
    DTF_LOG_WARN,
    DTF_LOG_ERROR,
} dtf_log_level_t;

/**
 * @brief PAL function pointer types — set non-NULL in dtf_config_t to override
 *        the weak-linked ESP32 defaults.
 */
typedef int      (*dtf_pal_storage_read_bsn_fn)(uint32_t *bsn);
typedef int      (*dtf_pal_storage_write_bsn_fn)(uint32_t bsn);
typedef int      (*dtf_pal_transport_send_fn)(const char *url, const char *auth_header,
                                               const uint8_t *payload, size_t len);
typedef uint32_t (*dtf_pal_clock_uptime_ms_fn)(void);
typedef int      (*dtf_pal_timer_create_fn)(uint32_t interval_ms,
                                             void (*callback)(void *), void *arg);

/**
 * @brief SDK configuration.  Zero-initialise with DTF_CONFIG_DEFAULT(), then
 *        set required fields before passing to dtf_init().
 */
typedef struct {
    /* Required */
    const char *api_key;    /**< Bearer token for the DTF ingest gateway. */
    const char *device_id;  /**< Unique device identifier. */

    /* Optional metadata */
    const char *fw_version; /**< Firmware version string (default: "unknown"). */
    const char *hw_variant; /**< Hardware variant string (default: "unknown"). */

    /* Feature flags */
    bool observability_enabled; /**< Enable log/metric ingestion (default: false). */

    /* Observability tuning — 0 or NULL uses menuconfig / compiled-in defaults */
    uint32_t    buffer_size;       /**< Max queued events before oldest are dropped. */
    uint32_t    flush_interval_ms; /**< Periodic flush interval in milliseconds. */
    const char *gateway_url;       /**< Ingest endpoint URL. */

    /* PAL overrides — NULL uses weak-linked ESP32 defaults */
    dtf_pal_storage_read_bsn_fn  storage_read_bsn;
    dtf_pal_storage_write_bsn_fn storage_write_bsn;
    dtf_pal_transport_send_fn    transport_send;
    dtf_pal_clock_uptime_ms_fn   clock_uptime_ms;
    dtf_pal_timer_create_fn      timer_create;
} dtf_config_t;

/**
 * @brief Zero-initialise the config struct with sane defaults.
 */
#define DTF_CONFIG_DEFAULT() {          \
    .api_key               = NULL,      \
    .device_id             = NULL,      \
    .fw_version            = NULL,      \
    .hw_variant            = NULL,      \
    .observability_enabled = false,     \
    .buffer_size           = 0,         \
    .flush_interval_ms     = 0,         \
    .gateway_url           = NULL,      \
    .storage_read_bsn      = NULL,      \
    .storage_write_bsn     = NULL,      \
    .transport_send        = NULL,      \
    .clock_uptime_ms       = NULL,      \
    .timer_create          = NULL,      \
}

#ifdef CONFIG_DTF_OBSERVABILITY

/**
 * @brief Initialise the DTF SDK.
 *
 * When observability_enabled is false (or dtf_init() is not called), all
 * observability code is inactive — no tasks, no memory, no network calls.
 *
 * @param config  Pointer to the configuration struct.
 * @return 0 on success, non-zero on error.
 */
int dtf_init(const dtf_config_t *config);

/**
 * @brief Queue a log event for delivery to the ingest gateway.
 *
 * Safe to call from any task.  No-op when observability is disabled or
 * dtf_init() has not been called.
 *
 * @param level    Severity level.
 * @param module   Subsystem tag (e.g. "wifi", "sensor").  May be NULL.
 * @param message  Human-readable message string.
 */
void dtf_log(dtf_log_level_t level, const char *module, const char *message);

/**
 * @brief Queue a gauge metric event for delivery to the ingest gateway.
 *
 * Safe to call from any task.  No-op when observability is disabled.
 *
 * @param name   Metric name (e.g. "hf", "rssi").
 * @param value  Signed 32-bit integer value.
 */
void dtf_metric(const char *name, int32_t value);

#else /* CONFIG_DTF_OBSERVABILITY not set — zero-overhead stubs */

static inline int dtf_init(const dtf_config_t *config)
{
    (void)config;
    return 0;
}

static inline void dtf_log(dtf_log_level_t level, const char *module,
                            const char *message)
{
    (void)level;
    (void)module;
    (void)message;
}

static inline void dtf_metric(const char *name, int32_t value)
{
    (void)name;
    (void)value;
}

#endif /* CONFIG_DTF_OBSERVABILITY */

#ifdef __cplusplus
}
#endif

#endif /* DTF_OBS_H */
