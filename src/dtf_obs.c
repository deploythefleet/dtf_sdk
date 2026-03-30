/*
 * dtf_obs.c — Platform-agnostic observability core.
 *
 * Zero esp_*.h includes. All platform dependencies are resolved through
 * the PAL (dtf_pal.h). Guarded by CONFIG_DTF_OBSERVABILITY so the entire
 * translation unit compiles away when observability is disabled.
 */

#include "dtf.h"
#include "dtf_pal.h"

#ifdef CONFIG_DTF_OBSERVABILITY

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * Event definitions
 * ------------------------------------------------------------------------- */

#define DTF_OBS_EVENT_MSG_MAX   128
#define DTF_OBS_EVENT_MOD_MAX   32
#define DTF_OBS_EVENT_NAME_MAX  16
#define DTF_OBS_LV_MAX          4

/* Compile-time ring-buffer capacity (Kconfig default: 32) */
#define DTF_OBS_BUFFER_MAX  CONFIG_DTF_OBS_BUFFER_SIZE

typedef enum {
    DTF_EVENT_TYPE_LOG,
    DTF_EVENT_TYPE_METRIC,
} dtf_event_type_t;

typedef struct {
    dtf_event_type_t type;
    uint32_t         bsn;
    uint32_t         up;
    union {
        struct {
            char lv[DTF_OBS_LV_MAX];
            char mod[DTF_OBS_EVENT_MOD_MAX];
            char msg[DTF_OBS_EVENT_MSG_MAX];
        } log;
        struct {
            char    name[DTF_OBS_EVENT_NAME_MAX];
            int32_t value;
        } metric;
    };
} dtf_event_t;

/* -------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */

typedef struct {
    dtf_config_t config;
    bool         initialized;
    uint32_t     bsn;

    /* Ring buffer */
    dtf_event_t buffer[DTF_OBS_BUFFER_MAX];
    uint32_t    head;   /* next write slot */
    uint32_t    tail;   /* oldest unread slot */
    uint32_t    count;  /* events currently in buffer */

    /* Mutex handle (opaque, owned by PAL) */
    void *mutex;

    /* Resolved PAL function pointers */
    int      (*storage_read_bsn)(uint32_t *);
    int      (*storage_write_bsn)(uint32_t);
    int      (*transport_send)(const char *, const char *,
                               const uint8_t *, size_t);
    uint32_t (*clock_uptime_ms)(void);
    int      (*timer_create)(uint32_t, void (*)(void *), void *);
} dtf_obs_state_t;

static dtf_obs_state_t obs_state;

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static const char *lv_to_str(dtf_log_level_t level)
{
    switch (level) {
    case DTF_LOG_DEBUG: return "d";
    case DTF_LOG_INFO:  return "i";
    case DTF_LOG_WARN:  return "w";
    case DTF_LOG_ERROR: return "e";
    default:            return "i";
    }
}

/*
 * Append a JSON-escaped string value (without surrounding quotes) to buf.
 * Returns the new position.
 */
static int json_escape(char *buf, int pos, int buf_max, const char *str)
{
    for (const char *c = str; *c != '\0' && pos < buf_max - 2; c++) {
        if (*c == '"' || *c == '\\') {
            if (pos < buf_max - 2) buf[pos++] = '\\';
        }
        buf[pos++] = *c;
    }
    return pos;
}

/* -------------------------------------------------------------------------
 * Flush — build JSON payload and hand to transport
 * ------------------------------------------------------------------------- */

static void obs_flush(void *arg)
{
    (void)arg;

    if (!obs_state.initialized) {
        return;
    }

    /* --- Critical section: snapshot and drain the ring buffer --- */
    dtf_pal_mutex_lock(obs_state.mutex);

    uint32_t count = obs_state.count;
    if (count == 0) {
        dtf_pal_mutex_unlock(obs_state.mutex);
        return;
    }

    dtf_event_t *events = malloc(count * sizeof(dtf_event_t));
    if (!events) {
        dtf_pal_mutex_unlock(obs_state.mutex);
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        events[i] = obs_state.buffer[(obs_state.tail + i) % DTF_OBS_BUFFER_MAX];
    }
    obs_state.tail  = 0;
    obs_state.head  = 0;
    obs_state.count = 0;

    dtf_pal_mutex_unlock(obs_state.mutex);

    /* --- Build JSON payload --- */

    /*
     * Conservative upper bound per event:
     *   log:    ~80 overhead + MOD_MAX + MSG_MAX = ~240 bytes
     *   metric: ~80 overhead + NAME_MAX         = ~96 bytes
     * Use 80 + max field sizes to cover both types.
     * Header:   ~200 bytes
     */
    int buf_max = 256 + (int)count * (80 + DTF_OBS_EVENT_MOD_MAX + DTF_OBS_EVENT_MSG_MAX);
    char *payload = malloc((size_t)buf_max);
    if (!payload) {
        free(events);
        return;
    }

    const char *device_id  = obs_state.config.device_id  ? obs_state.config.device_id  : "";
    const char *fw_version = obs_state.config.fw_version ? obs_state.config.fw_version : "";
    const char *hw_variant = obs_state.config.hw_variant ? obs_state.config.hw_variant : "";

    int pos = snprintf(payload, buf_max,
                       "{\"d\":\"%s\",\"fv\":\"%s\",\"hw\":\"%s\",\"e\":[",
                       device_id, fw_version, hw_variant);

    for (uint32_t i = 0; i < count && pos < buf_max - 4; i++) {
        const dtf_event_t *evt = &events[i];
        const char *sep = (i > 0) ? "," : "";

        if (evt->type == DTF_EVENT_TYPE_LOG) {
            pos += snprintf(payload + pos, buf_max - pos,
                            "%s{\"t\":\"l\",\"bsn\":%" PRIu32 ",\"up\":%" PRIu32
                            ",\"lv\":\"%s\"",
                            sep, evt->bsn, evt->up, evt->log.lv);

            if (evt->log.mod[0] != '\0') {
                pos += snprintf(payload + pos, buf_max - pos, ",\"mod\":\"");
                pos  = json_escape(payload, pos, buf_max, evt->log.mod);
                pos += snprintf(payload + pos, buf_max - pos, "\"");
            }

            pos += snprintf(payload + pos, buf_max - pos, ",\"m\":\"");
            pos  = json_escape(payload, pos, buf_max, evt->log.msg);
            pos += snprintf(payload + pos, buf_max - pos, "\"}");

        } else if (evt->type == DTF_EVENT_TYPE_METRIC) {
            pos += snprintf(payload + pos, buf_max - pos,
                            "%s{\"t\":\"m\",\"bsn\":%" PRIu32 ",\"up\":%" PRIu32
                            ",\"n\":\"",
                            sep, evt->bsn, evt->up);
            pos  = json_escape(payload, pos, buf_max, evt->metric.name);
            pos += snprintf(payload + pos, buf_max - pos,
                            "\",\"v\":%" PRId32 "}",
                            evt->metric.value);
        }
    }

    if (pos < buf_max - 2) {
        payload[pos++] = ']';
        payload[pos++] = '}';
        payload[pos]   = '\0';
    }

    free(events);

    /* --- Send --- */
    char auth_header[160];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s",
             obs_state.config.api_key ? obs_state.config.api_key : "");

    obs_state.transport_send(obs_state.config.gateway_url,
                             auth_header,
                             (const uint8_t *)payload,
                             (size_t)pos);

    free(payload);
}

/* -------------------------------------------------------------------------
 * Buffer helpers (must be called with mutex held)
 * ------------------------------------------------------------------------- */

static void buffer_push(const dtf_event_t *event)
{
    uint32_t cap = (obs_state.config.buffer_size > 0 &&
                    obs_state.config.buffer_size <= DTF_OBS_BUFFER_MAX)
                   ? obs_state.config.buffer_size
                   : DTF_OBS_BUFFER_MAX;

    if (obs_state.count >= cap) {
        /* Ring buffer: silently drop oldest event to make room */
        obs_state.tail = (obs_state.tail + 1) % DTF_OBS_BUFFER_MAX;
        obs_state.count--;
    }

    obs_state.buffer[obs_state.head] = *event;
    obs_state.head = (obs_state.head + 1) % DTF_OBS_BUFFER_MAX;
    obs_state.count++;
}

static bool buffer_is_full(void)
{
    uint32_t cap = (obs_state.config.buffer_size > 0 &&
                    obs_state.config.buffer_size <= DTF_OBS_BUFFER_MAX)
                   ? obs_state.config.buffer_size
                   : DTF_OBS_BUFFER_MAX;
    return obs_state.count >= cap;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

int dtf_init(const dtf_config_t *config)
{
    if (!config || !config->observability_enabled) {
        return 0;
    }

    /* Copy config */
    obs_state.config = *config;

    /* Apply compile-time defaults for unset optional fields */
    if (!obs_state.config.gateway_url) {
        obs_state.config.gateway_url = CONFIG_DTF_OBS_GATEWAY_URL;
    }
    if (obs_state.config.buffer_size == 0) {
        obs_state.config.buffer_size = CONFIG_DTF_OBS_BUFFER_SIZE;
    }
    if (obs_state.config.flush_interval_ms == 0) {
        obs_state.config.flush_interval_ms = CONFIG_DTF_OBS_FLUSH_INTERVAL_MS;
    }

    /* Resolve PAL function pointers (config override or weak-linked default) */
    obs_state.storage_read_bsn  = config->storage_read_bsn
                                   ? config->storage_read_bsn
                                   : dtf_pal_storage_read_bsn;
    obs_state.storage_write_bsn = config->storage_write_bsn
                                   ? config->storage_write_bsn
                                   : dtf_pal_storage_write_bsn;
    obs_state.transport_send    = config->transport_send
                                   ? config->transport_send
                                   : dtf_pal_transport_send;
    obs_state.clock_uptime_ms   = config->clock_uptime_ms
                                   ? config->clock_uptime_ms
                                   : dtf_pal_clock_uptime_ms;
    obs_state.timer_create      = config->timer_create
                                   ? config->timer_create
                                   : dtf_pal_timer_create;

    /* Create mutex */
    obs_state.mutex = dtf_pal_mutex_create();
    if (!obs_state.mutex) {
        return -1;
    }

    /* Read / increment / persist boot sequence number */
    uint32_t bsn = 0;
    if (obs_state.storage_read_bsn(&bsn) != 0) {
        bsn = 0; /* First boot or storage error — start at 0, will become 1 */
    }
    bsn++;
    obs_state.bsn = bsn;
    obs_state.storage_write_bsn(bsn); /* best-effort; ignore error */

    /* Initialize ring buffer */
    obs_state.head  = 0;
    obs_state.tail  = 0;
    obs_state.count = 0;

    obs_state.initialized = true;

    /* Start periodic flush */
    obs_state.timer_create(obs_state.config.flush_interval_ms, obs_flush, NULL);

    return 0;
}

int dtf_log(dtf_log_level_t level, const char *module, const char *message)
{
    if (!obs_state.initialized) {
        return -1;
    }

    dtf_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = DTF_EVENT_TYPE_LOG;
    event.bsn  = obs_state.bsn;
    event.up   = obs_state.clock_uptime_ms();

    strncpy(event.log.lv, lv_to_str(level), DTF_OBS_LV_MAX - 1);
    if (module) {
        strncpy(event.log.mod, module, DTF_OBS_EVENT_MOD_MAX - 1);
    }
    if (message) {
        strncpy(event.log.msg, message, DTF_OBS_EVENT_MSG_MAX - 1);
    }

    dtf_pal_mutex_lock(obs_state.mutex);
    buffer_push(&event);
    bool full = buffer_is_full();
    dtf_pal_mutex_unlock(obs_state.mutex);

    if (full) {
        obs_flush(NULL);
    }

    return 0;
}

int dtf_metric(const char *name, int32_t value)
{
    if (!obs_state.initialized) {
        return -1;
    }
    if (!name) {
        return -1;
    }

    dtf_event_t event;
    memset(&event, 0, sizeof(event));
    event.type         = DTF_EVENT_TYPE_METRIC;
    event.bsn          = obs_state.bsn;
    event.up           = obs_state.clock_uptime_ms();
    event.metric.value = value;
    strncpy(event.metric.name, name, DTF_OBS_EVENT_NAME_MAX - 1);

    dtf_pal_mutex_lock(obs_state.mutex);
    buffer_push(&event);
    bool full = buffer_is_full();
    dtf_pal_mutex_unlock(obs_state.mutex);

    if (full) {
        obs_flush(NULL);
    }

    return 0;
}

#else /* CONFIG_DTF_OBSERVABILITY not set — zero-overhead stubs */

int dtf_init(const dtf_config_t *config)
{
    (void)config;
    return 0;
}

int dtf_log(dtf_log_level_t level, const char *module, const char *message)
{
    (void)level;
    (void)module;
    (void)message;
    return 0;
}

int dtf_metric(const char *name, int32_t value)
{
    (void)name;
    (void)value;
    return 0;
}

#endif /* CONFIG_DTF_OBSERVABILITY */
