/*
 * dtf_obs.c — DTF observability core (platform-agnostic).
 *
 * No esp_*.h includes are permitted in this file.  All platform-specific
 * calls go through the PAL interfaces declared in dtf_pal.h.
 */

#include "dtf_obs.h"
#include "dtf_pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

/* Compile-time defaults — overridden by Kconfig when CONFIG_DTF_OBSERVABILITY=y */
#ifndef CONFIG_DTF_OBS_BUFFER_SIZE
#define CONFIG_DTF_OBS_BUFFER_SIZE 64
#endif

#ifndef CONFIG_DTF_OBS_FLUSH_INTERVAL_MS
#define CONFIG_DTF_OBS_FLUSH_INTERVAL_MS 30000
#endif

#ifndef CONFIG_DTF_OBS_GATEWAY_URL
#define CONFIG_DTF_OBS_GATEWAY_URL "https://ingest.deploythefleet.com/v1/ingest"
#endif

/* Per-event string length limits */
#define DTF_MOD_MAX_LEN  33   /* subsystem tag + NUL */
#define DTF_MSG_MAX_LEN  129  /* log message + NUL   */
#define DTF_NAME_MAX_LEN 17   /* metric name + NUL   */

/* Event type tags */
#define EVT_LOG    0
#define EVT_METRIC 1

typedef struct {
    uint8_t  type;
    uint32_t bsn;
    uint32_t up;
    union {
        struct {
            char lv[4];               /* "d", "i", "w", "e" + NUL */
            char mod[DTF_MOD_MAX_LEN];
            char m[DTF_MSG_MAX_LEN];
        } log;
        struct {
            char    n[DTF_NAME_MAX_LEN];
            int32_t v;
        } metric;
    };
} dtf_event_t;

/* ---------------------------------------------------------------------------
 * Module state
 * --------------------------------------------------------------------------- */

static bool s_obs_active = false;

static const char *s_api_key     = NULL;
static const char *s_device_id   = NULL;
static const char *s_fw_version  = NULL;
static const char *s_hw_variant  = NULL;
static const char *s_gateway_url = NULL;

static uint32_t s_bsn = 0;

/* Resolved PAL function pointers */
static dtf_pal_storage_read_bsn_fn  s_storage_read;
static dtf_pal_storage_write_bsn_fn s_storage_write;
static dtf_pal_transport_send_fn    s_transport;
static dtf_pal_clock_uptime_ms_fn   s_clock;
static dtf_pal_timer_create_fn      s_timer_create;

/* Ring buffer */
static dtf_event_t *s_ring     = NULL;
static uint32_t     s_ring_cap = 0;
static uint32_t     s_head     = 0;
static uint32_t     s_tail     = 0;
static uint32_t     s_count    = 0;

/* Concurrency */
static void *s_mutex    = NULL;
static bool  s_flushing = false;

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */

static const char *lv_str(dtf_log_level_t level)
{
    switch (level) {
        case DTF_LOG_DEBUG: return "d";
        case DTF_LOG_INFO:  return "i";
        case DTF_LOG_WARN:  return "w";
        case DTF_LOG_ERROR: return "e";
        default:            return "i";
    }
}

/* Append a JSON-escaped copy of src into dst[0..dst_cap-1].
 * Returns number of bytes written (not NUL-terminated). */
static size_t json_escape_append(char *dst, size_t dst_cap, const char *src)
{
    size_t n = 0;
    for (const char *p = src; *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') {
            if (n + 2 >= dst_cap) break;
            dst[n++] = '\\';
        } else {
            if (n + 1 >= dst_cap) break;
        }
        dst[n++] = *p;
    }
    return n;
}

/* ---------------------------------------------------------------------------
 * Ring buffer (caller must hold s_mutex)
 * --------------------------------------------------------------------------- */

static void buf_push(const dtf_event_t *evt)
{
    if (s_count == s_ring_cap) {
        /* Drop oldest event to make room */
        s_tail = (s_tail + 1) % s_ring_cap;
        s_count--;
        fprintf(stderr, "[dtf_obs] buffer full — oldest event dropped\n");
    }
    s_ring[s_head] = *evt;
    s_head = (s_head + 1) % s_ring_cap;
    s_count++;
}

/* Drain up to max_out events into out[].  Returns number drained. */
static uint32_t buf_drain(dtf_event_t *out, uint32_t max_out)
{
    uint32_t n = (s_count < max_out) ? s_count : max_out;
    for (uint32_t i = 0; i < n; i++) {
        out[i]  = s_ring[s_tail];
        s_tail  = (s_tail + 1) % s_ring_cap;
    }
    s_count -= n;
    return n;
}

/* ---------------------------------------------------------------------------
 * JSON serialisation
 * --------------------------------------------------------------------------- */

/* Per-event upper bound:
 *   log    ~300 bytes  (fixed fields + escaped mod + escaped message)
 *   metric ~120 bytes
 * Header: ~200 bytes; footer: 2 bytes. */
#define JSON_HEADER_RESERVE  200u
#define JSON_PER_EVENT_MAX   300u

static int build_payload(const dtf_event_t *evts, uint32_t count,
                          char **out_buf, size_t *out_len)
{
    size_t cap = JSON_HEADER_RESERVE + (size_t)count * JSON_PER_EVENT_MAX + 4;
    char  *buf = malloc(cap);
    if (!buf) return -1;

    const char *d  = s_device_id  ? s_device_id  : "";
    const char *fv = s_fw_version ? s_fw_version : "unknown";
    const char *hw = s_hw_variant ? s_hw_variant : "unknown";

    int pos = snprintf(buf, cap,
                       "{\"d\":\"%s\",\"fv\":\"%s\",\"hw\":\"%s\",\"e\":[",
                       d, fv, hw);
    if (pos < 0 || (size_t)pos >= cap) { free(buf); return -1; }

    for (uint32_t i = 0; i < count; i++) {
        if (i > 0 && (size_t)pos < cap) buf[pos++] = ',';

        const dtf_event_t *e = &evts[i];

        if (e->type == EVT_LOG) {
            int n = snprintf(buf + pos, cap - (size_t)pos,
                             "{\"t\":\"l\",\"bsn\":%" PRIu32 ",\"up\":%" PRIu32
                             ",\"lv\":\"%s\",\"mod\":\"",
                             e->bsn, e->up, e->log.lv);
            if (n < 0) { free(buf); return -1; }
            pos += n;
            pos += (int)json_escape_append(buf + pos, cap - (size_t)pos,
                                           e->log.mod);
            n = snprintf(buf + pos, cap - (size_t)pos, "\",\"m\":\"");
            if (n < 0) { free(buf); return -1; }
            pos += n;
            pos += (int)json_escape_append(buf + pos, cap - (size_t)pos,
                                           e->log.m);
            n = snprintf(buf + pos, cap - (size_t)pos, "\"}");
            if (n < 0) { free(buf); return -1; }
            pos += n;
        } else {
            int n = snprintf(buf + pos, cap - (size_t)pos,
                             "{\"t\":\"m\",\"bsn\":%" PRIu32 ",\"up\":%" PRIu32
                             ",\"n\":\"%s\",\"v\":%" PRId32 "}",
                             e->bsn, e->up, e->metric.n, e->metric.v);
            if (n < 0) { free(buf); return -1; }
            pos += n;
        }
    }

    if ((size_t)pos + 2 > cap) { free(buf); return -1; }
    buf[pos++] = ']';
    buf[pos++] = '}';
    buf[pos]   = '\0';

    *out_buf = buf;
    *out_len = (size_t)pos;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Flush (called from timer task or inline when buffer is full)
 * --------------------------------------------------------------------------- */

static void obs_flush(void *arg)
{
    (void)arg;

    if (!s_obs_active) return;

    /* Claim the flush slot — only one flush in flight at a time */
    dtf_pal_mutex_lock(s_mutex);
    if (s_flushing || s_count == 0) {
        dtf_pal_mutex_unlock(s_mutex);
        return;
    }
    s_flushing = true;

    uint32_t count = s_count;
    dtf_event_t *batch = malloc(sizeof(dtf_event_t) * count);
    if (!batch) {
        s_flushing = false;
        dtf_pal_mutex_unlock(s_mutex);
        return;
    }
    uint32_t drained = buf_drain(batch, count);
    dtf_pal_mutex_unlock(s_mutex);

    if (drained == 0) {
        free(batch);
        dtf_pal_mutex_lock(s_mutex);
        s_flushing = false;
        dtf_pal_mutex_unlock(s_mutex);
        return;
    }

    /* Build JSON payload (heap allocated, released after send) */
    char  *payload     = NULL;
    size_t payload_len = 0;
    if (build_payload(batch, drained, &payload, &payload_len) != 0) {
        free(batch);
        dtf_pal_mutex_lock(s_mutex);
        s_flushing = false;
        dtf_pal_mutex_unlock(s_mutex);
        return;
    }
    free(batch);

    /* Auth header: "Bearer <api_key>" */
    const char *prefix = "Bearer ";
    size_t auth_len = strlen(prefix) + strlen(s_api_key) + 1;
    char  *auth = malloc(auth_len);
    if (!auth) {
        free(payload);
        dtf_pal_mutex_lock(s_mutex);
        s_flushing = false;
        dtf_pal_mutex_unlock(s_mutex);
        return;
    }
    snprintf(auth, auth_len, "%s%s", prefix, s_api_key);

    int rc = s_transport(s_gateway_url, auth, (const uint8_t *)payload, payload_len);
    if (rc != 0) {
        /* M1: log and discard — no retry */
        fprintf(stderr, "[dtf_obs] transport error %d — batch discarded\n", rc);
    }

    free(auth);
    free(payload);

    dtf_pal_mutex_lock(s_mutex);
    s_flushing = false;
    dtf_pal_mutex_unlock(s_mutex);
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

int dtf_init(const dtf_config_t *config)
{
    if (!config) return -1;

    s_api_key    = config->api_key;
    s_device_id  = config->device_id;
    s_fw_version = config->fw_version;
    s_hw_variant = config->hw_variant;

    if (!config->observability_enabled) return 0;

    if (!s_api_key || !s_device_id) return -1;

    /* Resolve PAL: config callback takes priority over weak-linked default */
    s_storage_read  = config->storage_read_bsn  ? config->storage_read_bsn
                                                 : dtf_pal_storage_read_bsn;
    s_storage_write = config->storage_write_bsn ? config->storage_write_bsn
                                                 : dtf_pal_storage_write_bsn;
    s_transport     = config->transport_send     ? config->transport_send
                                                 : dtf_pal_transport_send;
    s_clock         = config->clock_uptime_ms    ? config->clock_uptime_ms
                                                 : dtf_pal_clock_uptime_ms;
    s_timer_create  = config->timer_create       ? config->timer_create
                                                 : dtf_pal_timer_create;

    s_gateway_url = (config->gateway_url && config->gateway_url[0])
                  ? config->gateway_url
                  : CONFIG_DTF_OBS_GATEWAY_URL;

    /* BSN: read from storage, increment, persist */
    if (s_storage_read(&s_bsn) != 0) s_bsn = 0;
    s_bsn++;
    s_storage_write(s_bsn); /* best-effort; ignore error */

    /* Allocate ring buffer */
    uint32_t ring_cap = (config->buffer_size > 0)
                       ? config->buffer_size
                       : (uint32_t)CONFIG_DTF_OBS_BUFFER_SIZE;
    s_ring = calloc(ring_cap, sizeof(dtf_event_t));
    if (!s_ring) return -1;
    s_ring_cap = ring_cap;
    s_head = s_tail = s_count = 0;

    /* Create thread-safety mutex */
    s_mutex = dtf_pal_mutex_create();
    if (!s_mutex) {
        free(s_ring);
        s_ring = NULL;
        return -1;
    }

    /* Start periodic flush timer */
    uint32_t interval = (config->flush_interval_ms > 0)
                       ? config->flush_interval_ms
                       : (uint32_t)CONFIG_DTF_OBS_FLUSH_INTERVAL_MS;
    if (s_timer_create(interval, obs_flush, NULL) != 0) {
        free(s_ring);
        s_ring = NULL;
        return -1;
    }

    s_obs_active = true;
    return 0;
}

void dtf_log(dtf_log_level_t level, const char *module, const char *message)
{
    if (!s_obs_active) return;

    dtf_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = EVT_LOG;
    evt.bsn  = s_bsn;
    evt.up   = s_clock();
    strncpy(evt.log.lv, lv_str(level), sizeof(evt.log.lv) - 1);
    if (module)  strncpy(evt.log.mod, module,  sizeof(evt.log.mod)  - 1);
    if (message) strncpy(evt.log.m,   message, sizeof(evt.log.m)    - 1);

    dtf_pal_mutex_lock(s_mutex);
    buf_push(&evt);
    bool full = (s_count == s_ring_cap);
    dtf_pal_mutex_unlock(s_mutex);

    if (full) obs_flush(NULL);
}

void dtf_metric(const char *name, int32_t value)
{
    if (!s_obs_active || !name) return;

    dtf_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type     = EVT_METRIC;
    evt.bsn      = s_bsn;
    evt.up       = s_clock();
    strncpy(evt.metric.n, name, sizeof(evt.metric.n) - 1);
    evt.metric.v = value;

    dtf_pal_mutex_lock(s_mutex);
    buf_push(&evt);
    bool full = (s_count == s_ring_cap);
    dtf_pal_mutex_unlock(s_mutex);

    if (full) obs_flush(NULL);
}
