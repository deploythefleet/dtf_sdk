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
    DTF_LOG_VERBOSE = 0,
    DTF_LOG_DEBUG,
    DTF_LOG_INFO,
    DTF_LOG_WARN,
    DTF_LOG_ERROR,
} dtf_log_level_t;

/**
 * @brief PAL function pointer types — set non-NULL in dtf_config_t to override
 *        the weak-linked ESP32 defaults at runtime.
 *
 * Only functions with legitimate runtime-swap use cases are exposed here.
 * Platform internals (clock, timer, mutex) are overridable only via
 * weak-linked PAL definitions at compile time.
 */
typedef int (*dtf_pal_read_bsn_fn)(uint32_t *bsn);
typedef int (*dtf_pal_write_bsn_fn)(uint32_t bsn);
typedef int (*dtf_pal_transport_send_fn)(const char *url, const char *auth_header,
                                         const uint8_t *payload, size_t len);

/**
 * @brief SDK configuration.  Zero-initialise with DTF_CONFIG_DEFAULT(), then
 *        set required fields before passing to dtf_init().
 */
typedef struct {
    /* Required */
    const char *api_key;    /**< Bearer token for the DTF ingest gateway. */

    /* Optional metadata — NULL or empty uses platform-derived defaults */
    const char *device_id;  /**< Device ID (default: MAC address, e.g. "aabbccddeeff"). */
    const char *fw_version; /**< Firmware version (default: PROJECT_VER from CMakeLists.txt). */
    const char *hw_variant; /**< Hardware variant (default: chip type, e.g. "esp32s3"). */

    /* Observability tuning — 0 or NULL uses menuconfig / compiled-in defaults */
    uint32_t    flush_interval_ms;   /**< Periodic flush interval in milliseconds. */
    const char *gateway_url;         /**< Ingest endpoint URL. */

    /* PAL overrides — NULL uses weak-linked ESP32 defaults.
     * For platform internals (clock, timer, mutex), override the
     * weak-linked functions in dtf_pal.h directly instead. */
    dtf_pal_read_bsn_fn        read_bsn;
    dtf_pal_write_bsn_fn       write_bsn;
    dtf_pal_transport_send_fn  transport_send;
} dtf_config_t;

/**
 * @brief Zero-initialise the config struct with sane defaults.
 */
#define DTF_CONFIG_DEFAULT() {          \
    .api_key               = NULL,      \
    .device_id             = NULL,      \
    .fw_version            = NULL,      \
    .hw_variant            = NULL,      \
    .flush_interval_ms     = 0,         \
    .gateway_url           = NULL,      \
    .read_bsn              = NULL,      \
    .write_bsn             = NULL,      \
    .transport_send        = NULL,      \
}

#ifdef CONFIG_DTF_OBSERVABILITY

/**
 * @brief Initialise the DTF SDK.
 *
 * Call once at startup. Observability is controlled by the Kconfig option
 * DTF_OBSERVABILITY — when disabled, all observability code is excluded
 * at compile time (zero binary size impact).
 *
 * @param config  Pointer to the configuration struct.
 * @return 0 on success, non-zero on error.
 */
int dtf_init(const dtf_config_t *config);

/**
 * @brief Queue a log event for delivery to the ingest gateway.
 *
 * Safe to call from any task.  No-op if dtf_init() has not been called.
 *
 * @param level    Severity level.
 * @param module   Subsystem tag (e.g. "wifi", "sensor").  May be NULL.
 * @param message  Human-readable message string.
 */
void dtf_log(dtf_log_level_t level, const char *module, const char *message);

/**
 * @brief Queue a gauge metric event (integer) for delivery to the ingest gateway.
 *
 * Safe to call from any task.  No-op if dtf_init() has not been called.
 *
 * @param name   Metric name (e.g. "hf", "rssi").
 * @param value  Signed 32-bit integer value.
 */
void dtf_metric(const char *name, int32_t value);

#ifdef CONFIG_DTF_OBS_FLOAT_METRICS
/**
 * @brief Queue a gauge metric event (float) for delivery to the ingest gateway.
 *
 * Requires CONFIG_DTF_OBS_FLOAT_METRICS=y. On targets without an FPU, enabling
 * this will pull in the software floating-point library and increase binary size.
 *
 * Safe to call from any task.  No-op if dtf_init() has not been called.
 *
 * @param name   Metric name (e.g. "temp", "voltage").
 * @param value  Single-precision float value.
 */
void dtf_metric_float(const char *name, float value);
#endif

/**
 * @brief Drain the ingest queue into the persist layer.
 *
 * Fast, no network I/O, always safe, idempotent.
 * In managed mode, this is called automatically by the background task.
 * Call this in manual mode to persist queued events before sleeping.
 */
void dtf_process(void);

/**
 * @brief Persist pending events, then transmit one chunk to the remote endpoint.
 *
 * Calls dtf_process() internally. No cooldown — each call sends immediately
 * if data is available.
 *
 * @return 0 = chunk sent, 1 = nothing to send, -1 = transport error.
 */
int dtf_send(void);

/**
 * @brief Returns the number of bytes pending in the persist layer.
 *
 * Use this to decide whether to connect to the network or to drive
 * a drain loop with dtf_send().
 */
size_t dtf_pending(void);

#else /* CONFIG_DTF_OBSERVABILITY not set — zero-overhead stubs with compiler warnings */

#define DTF_OBS_DISABLED_WARNING __attribute__((warning( \
    "DTF observability call present but CONFIG_DTF_OBSERVABILITY is disabled. " \
    "Enable it in menuconfig or suppress this warning if intentional.")))

static inline int dtf_init(const dtf_config_t *config)
{
    (void)config;
    return 0;
}

DTF_OBS_DISABLED_WARNING
static inline void dtf_log(dtf_log_level_t level, const char *module,
                            const char *message)
{
    (void)level;
    (void)module;
    (void)message;
}

DTF_OBS_DISABLED_WARNING
static inline void dtf_metric(const char *name, int32_t value)
{
    (void)name;
    (void)value;
}

DTF_OBS_DISABLED_WARNING
static inline void dtf_metric_float(const char *name, float value)
{
    (void)name;
    (void)value;
}

static inline void dtf_process(void) {}

static inline int dtf_send(void)
{
    return 1;
}

static inline size_t dtf_pending(void)
{
    return 0;
}

#undef DTF_OBS_DISABLED_WARNING

#endif /* CONFIG_DTF_OBSERVABILITY */

#ifdef __cplusplus
}
#endif

#endif /* DTF_OBS_H */
