/*
 * dtf_obs.c — DTF observability core (platform-agnostic).
 *
 * No esp_*.h includes are permitted in this file.  All platform-specific
 * calls go through the PAL interfaces declared in dtf_pal.h.
 */

#include "dtf_obs.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dtf_pal.h"

/* Compile-time defaults — overridden by Kconfig when CONFIG_DTF_OBSERVABILITY=y */
#ifndef CONFIG_DTF_OBS_PERSIST_BUFFER_SIZE
#define CONFIG_DTF_OBS_PERSIST_BUFFER_SIZE 8192
#endif

#ifndef CONFIG_DTF_OBS_TX_BUFFER_SIZE
#define CONFIG_DTF_OBS_TX_BUFFER_SIZE 2048
#endif

#ifndef CONFIG_DTF_OBS_FLUSH_INTERVAL_MS
#define CONFIG_DTF_OBS_FLUSH_INTERVAL_MS 10000
#endif

#ifndef CONFIG_DTF_OBS_GATEWAY_URL
#define CONFIG_DTF_OBS_GATEWAY_URL "https://ingest.deploythefleet.com/v1/ingest"
#endif

/* Per-event string length limits */
#define DTF_MOD_MAX_LEN 33  /* subsystem tag + NUL */
#define DTF_MSG_MAX_LEN 129 /* log message + NUL   */
#define DTF_NAME_MAX_LEN 17 /* metric name + NUL   */

/* Auth header max: "Bearer " (7) + key + NUL */
#define DTF_AUTH_MAX_LEN 128

/* Event type tags */
#define EVT_LOG 0
#define EVT_METRIC 1
#define EVT_METRIC_FLOAT 2

typedef struct {
  uint8_t type;
  uint32_t bsn;
  uint32_t up;
  union {
    struct {
      char lv[4]; /* "d", "i", "w", "e" + NUL */
      char mod[DTF_MOD_MAX_LEN];
      char m[DTF_MSG_MAX_LEN];
    } log;
    struct {
      char n[DTF_NAME_MAX_LEN];
      int32_t v;
    } metric;
#ifdef CONFIG_DTF_OBS_FLOAT_METRICS
    struct {
      char n[DTF_NAME_MAX_LEN];
      float v;
    } metric_float;
#endif
  };
} dtf_event_t;

/* Derive ring capacity from persist buffer size */
#define RING_CAP (CONFIG_DTF_OBS_PERSIST_BUFFER_SIZE / sizeof(dtf_event_t))

/* Compile-time check: buffer must hold at least 2 events */
_Static_assert(RING_CAP >= 2, "DTF_OBS_PERSIST_BUFFER_SIZE too small — must hold at least 2 events");

/* ---------------------------------------------------------------------------
 * Static buffers — no heap allocation
 * --------------------------------------------------------------------------- */

static dtf_event_t s_ring[RING_CAP];
static uint8_t s_tx_buf[CONFIG_DTF_OBS_TX_BUFFER_SIZE];
static char s_auth[DTF_AUTH_MAX_LEN];

/* ---------------------------------------------------------------------------
 * Module state
 * --------------------------------------------------------------------------- */

static bool s_obs_active = false;

static const char* s_device_id = NULL;
static const char* s_fw_version = NULL;
static const char* s_hw_variant = NULL;
static const char* s_gateway_url = NULL;

static uint32_t s_bsn = 0;
static uint32_t s_drop_count = 0;

/* Resolved PAL function pointers (config-overridable) */
static dtf_pal_read_bsn_fn s_read_bsn;
static dtf_pal_write_bsn_fn s_write_bsn;
static dtf_pal_transport_send_fn s_transport;

/* Ring buffer */
static uint32_t s_ring_cap = 0;
static uint32_t s_head = 0;
static uint32_t s_tail = 0;
static uint32_t s_count = 0;

/* Concurrency */
static void* s_mutex = NULL;
static bool s_sending = false;

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */

static const char* lv_str(dtf_log_level_t level) {
  switch (level) {
    case DTF_LOG_VERBOSE:
      return "v";
    case DTF_LOG_DEBUG:
      return "d";
    case DTF_LOG_INFO:
      return "i";
    case DTF_LOG_WARN:
      return "w";
    case DTF_LOG_ERROR:
      return "e";
    default:
      return "i";
  }
}

/* ---------------------------------------------------------------------------
 * Minimal CBOR encoder (RFC 8949 — no library dependency)
 * --------------------------------------------------------------------------- */

static size_t cbor_encode_uint_hdr(uint8_t* buf, uint8_t major, uint64_t val) {
  major <<= 5;
  if (val <= 23) {
    buf[0] = major | (uint8_t)val;
    return 1;
  }
  if (val <= 0xFF) {
    buf[0] = major | 24;
    buf[1] = (uint8_t)val;
    return 2;
  }
  if (val <= 0xFFFF) {
    buf[0] = major | 25;
    buf[1] = (uint8_t)(val >> 8);
    buf[2] = (uint8_t)val;
    return 3;
  }
  /* uint32 is sufficient for this SDK */
  buf[0] = major | 26;
  buf[1] = (uint8_t)(val >> 24);
  buf[2] = (uint8_t)(val >> 16);
  buf[3] = (uint8_t)(val >> 8);
  buf[4] = (uint8_t)val;
  return 5;
}

static size_t cbor_encode_int(uint8_t* buf, int32_t val) {
  if (val >= 0) return cbor_encode_uint_hdr(buf, 0, (uint64_t)val);
  return cbor_encode_uint_hdr(buf, 1, (uint64_t)(-(int64_t)val - 1));
}

static size_t cbor_encode_tstr(uint8_t* buf, const char* str) {
  size_t slen = strlen(str);
  size_t hdr = cbor_encode_uint_hdr(buf, 3, slen);
  memcpy(buf + hdr, str, slen);
  return hdr + slen;
}

static size_t cbor_encode_map_hdr(uint8_t* buf, uint32_t n_pairs) {
  return cbor_encode_uint_hdr(buf, 5, n_pairs);
}

static size_t cbor_encode_array_hdr(uint8_t* buf, uint32_t n_items) {
  return cbor_encode_uint_hdr(buf, 4, n_items);
}

static size_t cbor_kv_tstr(uint8_t* buf, const char* key, const char* val) {
  size_t n = cbor_encode_tstr(buf, key);
  n += cbor_encode_tstr(buf + n, val);
  return n;
}

static size_t cbor_kv_uint(uint8_t* buf, const char* key, uint32_t val) {
  size_t n = cbor_encode_tstr(buf, key);
  n += cbor_encode_uint_hdr(buf + n, 0, val);
  return n;
}

static size_t cbor_kv_int(uint8_t* buf, const char* key, int32_t val) {
  size_t n = cbor_encode_tstr(buf, key);
  n += cbor_encode_int(buf + n, val);
  return n;
}

#ifdef CONFIG_DTF_OBS_FLOAT_METRICS
/* Encode a single-precision float (CBOR major type 7, additional info 26) */
static size_t cbor_encode_float(uint8_t* buf, float val) {
  uint32_t bits;
  memcpy(&bits, &val, sizeof(bits));
  buf[0] = 0xFA; /* float32 */
  buf[1] = (uint8_t)(bits >> 24);
  buf[2] = (uint8_t)(bits >> 16);
  buf[3] = (uint8_t)(bits >> 8);
  buf[4] = (uint8_t)bits;
  return 5;
}

static size_t cbor_kv_float(uint8_t* buf, const char* key, float val) {
  size_t n = cbor_encode_tstr(buf, key);
  n += cbor_encode_float(buf + n, val);
  return n;
}
#endif

/* ---------------------------------------------------------------------------
 * Ring buffer (caller must hold s_mutex)
 * --------------------------------------------------------------------------- */

static void ring_push(const dtf_event_t* evt) {
  if (s_count == s_ring_cap) {
    s_tail = (s_tail + 1) % s_ring_cap;
    s_count--;
    s_drop_count++;
    if (s_drop_count == 1 || s_drop_count == 10 || s_drop_count == 100 || s_drop_count % 1000 == 0) {
      fprintf(stderr,
              "[dtf_obs] event dropped (%lu total since last send)"
              " — increase DTF_OBS_PERSIST_BUFFER_SIZE or send more frequently\n",
              (unsigned long)s_drop_count);
    }
  }
  s_ring[s_head] = *evt;
  s_head = (s_head + 1) % s_ring_cap;
  s_count++;
}

/* ---------------------------------------------------------------------------
 * CBOR serialisation — encodes events from ring into s_tx_buf
 *
 * Returns the number of events encoded, or -1 on error.
 * Sets *out_len to the total bytes written into s_tx_buf.
 * Sets *out_events_consumed to the number of ring slots consumed.
 * --------------------------------------------------------------------------- */

/* Upper bound for a single event's CBOR encoding */
#define CBOR_PER_EVENT_MAX 220u

/* Encode a single event into buf. Returns bytes written. */
static size_t cbor_encode_event(uint8_t* buf, const dtf_event_t* e) {
  size_t pos = 0;
  if (e->type == EVT_LOG) {
    pos += cbor_encode_map_hdr(buf + pos, 6);
    pos += cbor_kv_tstr(buf + pos, "t", "l");
    pos += cbor_kv_uint(buf + pos, "bsn", e->bsn);
    pos += cbor_kv_uint(buf + pos, "up", e->up);
    pos += cbor_kv_tstr(buf + pos, "lv", e->log.lv);
    pos += cbor_kv_tstr(buf + pos, "mod", e->log.mod);
    pos += cbor_kv_tstr(buf + pos, "m", e->log.m);
  }
  else if (e->type == EVT_METRIC) {
    pos += cbor_encode_map_hdr(buf + pos, 5);
    pos += cbor_kv_tstr(buf + pos, "t", "m");
    pos += cbor_kv_uint(buf + pos, "bsn", e->bsn);
    pos += cbor_kv_uint(buf + pos, "up", e->up);
    pos += cbor_kv_tstr(buf + pos, "n", e->metric.n);
    pos += cbor_kv_int(buf + pos, "v", e->metric.v);
  }
#ifdef CONFIG_DTF_OBS_FLOAT_METRICS
  else if (e->type == EVT_METRIC_FLOAT) {
    pos += cbor_encode_map_hdr(buf + pos, 5);
    pos += cbor_kv_tstr(buf + pos, "t", "m");
    pos += cbor_kv_uint(buf + pos, "bsn", e->bsn);
    pos += cbor_kv_uint(buf + pos, "up", e->up);
    pos += cbor_kv_tstr(buf + pos, "n", e->metric_float.n);
    pos += cbor_kv_float(buf + pos, "v", e->metric_float.v);
  }
#endif
  return pos;
}

/*
 * Build a CBOR payload into s_tx_buf from events in the ring.
 * Encodes as many events as fit. Does NOT consume ring entries —
 * the caller must advance s_tail after successful transmission.
 *
 * Returns the number of events included, sets *out_len to payload size.
 */
static uint32_t build_payload(size_t* out_len) {
  const char* d = s_device_id;
  const char* fv = s_fw_version;
  const char* hw = s_hw_variant;
  size_t cap = sizeof(s_tx_buf);

  /* Encode the envelope header into a scratch area to measure its size.
   * We need to know how many bytes the header takes before we know
   * how many events fit. Use the end of s_tx_buf as scratch. */
  uint8_t hdr_scratch[128];
  size_t hdr_len = 0;
  uint32_t map_pairs = s_drop_count > 0 ? 6 : 5;
  hdr_len += cbor_encode_map_hdr(hdr_scratch + hdr_len, map_pairs);
  hdr_len += cbor_kv_tstr(hdr_scratch + hdr_len, "src", "device");
  hdr_len += cbor_kv_tstr(hdr_scratch + hdr_len, "d", d);
  hdr_len += cbor_kv_tstr(hdr_scratch + hdr_len, "fv", fv);
  hdr_len += cbor_kv_tstr(hdr_scratch + hdr_len, "hw", hw);
  if (s_drop_count > 0) {
    hdr_len += cbor_kv_uint(hdr_scratch + hdr_len, "dropped", s_drop_count);
  }
  hdr_len += cbor_encode_tstr(hdr_scratch + hdr_len, "e");
  hdr_len += cbor_encode_array_hdr(hdr_scratch + hdr_len, 0); /* placeholder for size estimate */

  if (hdr_len >= cap) {
    *out_len = 0;
    return 0;
  }

  /* Trial-encode events to count how many fit */
  uint8_t evt_scratch[CBOR_PER_EVENT_MAX];
  uint32_t evt_count = 0;
  size_t evt_total_bytes = 0;
  size_t available = cap - hdr_len;
  uint32_t idx = s_tail;

  for (uint32_t i = 0; i < s_count; i++) {
    size_t evt_len = cbor_encode_event(evt_scratch, &s_ring[idx]);
    if (evt_total_bytes + evt_len > available) break;
    evt_total_bytes += evt_len;
    evt_count++;
    idx = (idx + 1) % s_ring_cap;
  }

  if (evt_count == 0) {
    *out_len = 0;
    return 0;
  }

  /* Now write the real payload into s_tx_buf */
  size_t pos = 0;
  pos += cbor_encode_map_hdr(s_tx_buf + pos, map_pairs);
  pos += cbor_kv_tstr(s_tx_buf + pos, "src", "device");
  pos += cbor_kv_tstr(s_tx_buf + pos, "d", d);
  pos += cbor_kv_tstr(s_tx_buf + pos, "fv", fv);
  pos += cbor_kv_tstr(s_tx_buf + pos, "hw", hw);
  if (s_drop_count > 0) {
    pos += cbor_kv_uint(s_tx_buf + pos, "dropped", s_drop_count);
  }
  pos += cbor_encode_tstr(s_tx_buf + pos, "e");
  pos += cbor_encode_array_hdr(s_tx_buf + pos, evt_count);

  idx = s_tail;
  for (uint32_t i = 0; i < evt_count; i++) {
    pos += cbor_encode_event(s_tx_buf + pos, &s_ring[idx]);
    idx = (idx + 1) % s_ring_cap;
  }

  *out_len = pos;
  return evt_count;
}

/* ---------------------------------------------------------------------------
 * Pipeline timer callback (managed mode)
 * --------------------------------------------------------------------------- */

static void pipeline_tick(void* arg) {
  (void)arg;
  dtf_send();
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

int dtf_init(const dtf_config_t* config) {
  if (!config) return -1;

  s_device_id = (config->device_id && config->device_id[0]) ? config->device_id : dtf_pal_get_device_id();
  s_fw_version = (config->fw_version && config->fw_version[0]) ? config->fw_version : dtf_pal_get_fw_version();
  s_hw_variant = (config->hw_variant && config->hw_variant[0]) ? config->hw_variant : dtf_pal_get_hw_variant();

  if (!config->api_key) return -1;

  /* Pre-build auth header */
  int n = snprintf(s_auth, sizeof(s_auth), "Bearer %s", config->api_key);
  if (n < 0 || (size_t)n >= sizeof(s_auth)) return -1;

  /* Resolve config-overridable PAL functions */
  s_read_bsn = config->read_bsn ? config->read_bsn : dtf_pal_read_bsn;
  s_write_bsn = config->write_bsn ? config->write_bsn : dtf_pal_write_bsn;
  s_transport = config->transport_send ? config->transport_send : dtf_pal_transport_send;

  s_gateway_url = (config->gateway_url && config->gateway_url[0]) ? config->gateway_url : CONFIG_DTF_OBS_GATEWAY_URL;

  /* BSN: read from storage, increment, persist */
  if (s_read_bsn(&s_bsn) != 0) s_bsn = 0;
  s_bsn++;
  s_write_bsn(s_bsn); /* best-effort; ignore error */

  /* Reset ring buffer state */
  s_ring_cap = RING_CAP;
  s_head = s_tail = s_count = 0;
  s_drop_count = 0;

  /* Create thread-safety mutex */
  s_mutex = dtf_pal_mutex_create();
  if (!s_mutex) return -1;

  /* Start periodic pipeline timer */
  uint32_t interval =
      (config->flush_interval_ms > 0) ? config->flush_interval_ms : (uint32_t)CONFIG_DTF_OBS_FLUSH_INTERVAL_MS;
  if (dtf_pal_timer_create(interval, pipeline_tick, NULL) != 0) return -1;

  s_obs_active = true;
  return 0;
}

void dtf_log(dtf_log_level_t level, const char* module, const char* message) {
  if (!s_obs_active) return;

  dtf_event_t evt;
  memset(&evt, 0, sizeof(evt));
  evt.type = EVT_LOG;
  evt.bsn = s_bsn;
  evt.up = dtf_pal_clock_uptime_ms();
  strncpy(evt.log.lv, lv_str(level), sizeof(evt.log.lv) - 1);
  if (module) strncpy(evt.log.mod, module, sizeof(evt.log.mod) - 1);
  if (message) strncpy(evt.log.m, message, sizeof(evt.log.m) - 1);

  dtf_pal_mutex_lock(s_mutex);
  ring_push(&evt);
  dtf_pal_mutex_unlock(s_mutex);
}

void dtf_metric(const char* name, int32_t value) {
  if (!s_obs_active || !name) return;

  dtf_event_t evt;
  memset(&evt, 0, sizeof(evt));
  evt.type = EVT_METRIC;
  evt.bsn = s_bsn;
  evt.up = dtf_pal_clock_uptime_ms();
  strncpy(evt.metric.n, name, sizeof(evt.metric.n) - 1);
  evt.metric.v = value;

  dtf_pal_mutex_lock(s_mutex);
  ring_push(&evt);
  dtf_pal_mutex_unlock(s_mutex);
}

#ifdef CONFIG_DTF_OBS_FLOAT_METRICS
void dtf_metric_float(const char* name, float value) {
  if (!s_obs_active || !name) return;

  dtf_event_t evt;
  memset(&evt, 0, sizeof(evt));
  evt.type = EVT_METRIC_FLOAT;
  evt.bsn = s_bsn;
  evt.up = dtf_pal_clock_uptime_ms();
  strncpy(evt.metric_float.n, name, sizeof(evt.metric_float.n) - 1);
  evt.metric_float.v = value;

  dtf_pal_mutex_lock(s_mutex);
  ring_push(&evt);
  dtf_pal_mutex_unlock(s_mutex);
}
#endif

void dtf_process(void) {
  /* V1: ingest and persist are the same ring buffer — nothing to drain.
   * This function exists to establish the API contract. When the byte ring
   * and separate ingest queue are implemented, this will drain ingest → persist. */
}

int dtf_send(void) {
  if (!s_obs_active) return 1;

  dtf_process();

  dtf_pal_mutex_lock(s_mutex);
  if (s_sending || s_count == 0) {
    dtf_pal_mutex_unlock(s_mutex);
    return 1;
  }
  s_sending = true;

  /* Build payload from ring (does not consume entries yet) */
  size_t payload_len = 0;
  uint32_t evt_count = build_payload(&payload_len);
  uint32_t dropped_snapshot = s_drop_count;
  dtf_pal_mutex_unlock(s_mutex);

  if (evt_count == 0 || payload_len == 0) {
    dtf_pal_mutex_lock(s_mutex);
    s_sending = false;
    dtf_pal_mutex_unlock(s_mutex);
    return 1;
  }

  /* Transmit */
  int rc = s_transport(s_gateway_url, s_auth, s_tx_buf, payload_len);

  dtf_pal_mutex_lock(s_mutex);
  if (rc == 0) {
    /* Success — consume the events we sent */
    s_tail = (s_tail + evt_count) % s_ring_cap;
    s_count -= evt_count;
    /* Reset drop counter only if no new drops occurred during send */
    if (s_drop_count == dropped_snapshot) {
      s_drop_count = 0;
    }
  }
  else {
    fprintf(stderr, "[dtf_obs] transport error %d — will retry\n", rc);
  }
  s_sending = false;
  dtf_pal_mutex_unlock(s_mutex);

  return rc == 0 ? 0 : -1;
}

size_t dtf_pending(void) {
  if (!s_obs_active) return 0;
  dtf_pal_mutex_lock(s_mutex);
  size_t bytes = s_count * sizeof(dtf_event_t);
  dtf_pal_mutex_unlock(s_mutex);
  return bytes;
}
