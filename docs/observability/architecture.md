# DTF Observability Pipeline — Architecture Specification

## 1. Overview

The DTF observability subsystem captures logs, metrics, and (future) structured events from ESP32 firmware and delivers them to a remote ingest endpoint. It is designed for constrained embedded environments where memory, power, and connectivity are limited and unpredictable.

### Design principles

- **Zero-config by default.** A user provides an API key, calls `dtf_init()`, and observability works. Observability is enabled by default in `DTF_CONFIG_DEFAULT()`, device ID is derived from the MAC address, and hardware variant is derived from the chip type. No tuning required.
- **Fully configurable when needed.** Every internal default can be overridden via Kconfig or PAL function replacement for users who need to optimize for their specific hardware, connectivity, and power constraints.
- **Predictable resource usage.** The SDK's memory footprint is statically determined at build time. No heap allocation in the default configuration. The user declares a byte budget and the SDK operates within it.
- **Implementation complexity is hidden.** The internal architecture may be sophisticated, but the user-facing API is simple: events go in, telemetry comes out.

### Pipeline model

All observability data flows through a three-stage pipeline:

```
Ingest → Persist → Transmit
```

Each stage has a single responsibility, a default implementation, and a PAL interface for replacement. The stages are decoupled — a slow transport never blocks event ingestion, and a fast producer never overwhelms the network.

## 2. Pipeline stages

### 2.1 Ingest

**Responsibility:** Accept events from application code and encode them for storage.

**Inputs:**
- `dtf_log()` — structured log messages with level, module, and message text
- `dtf_metric()` — key/value numeric measurements
- (Future) ESP-IDF log hook — captures `ESP_LOGx()` output transparently
- (Future) `dtf_event()` — arbitrary structured events

**Behavior:**
- Events are CBOR-encoded at ingest time into a compact binary representation.
- Encoded events are pushed into a small static ingest queue (RAM only, always).
- The ingest queue is a shock absorber — it decouples the caller from the persist layer so that `dtf_log()` and `dtf_metric()` never block, regardless of what the persist backend is doing.
- If the ingest queue is full, the oldest entry is dropped and the module-level drop counter is incremented.
- String fields (message, module, metric name) are truncated to configured maximums at this stage. Data that doesn't fit is clipped, not rejected.

**Thread safety:** All ingest functions are safe to call from any task context. (ISR support is a future extension — see section 10.3.)

### 2.2 Persist

**Responsibility:** Durably store encoded events until they can be transmitted.

**Inputs:** CBOR-encoded events from the ingest queue, written via the persist PAL as opaque byte sequences with 2-byte length framing.

**Default implementation:** RAM byte ring (see section 8 for internals).

**Alternative implementations (user-provided via PAL):**
- NVS (small event volumes, survives deep sleep)
- LittleFS / SPIFFS (large volumes, flash-backed)
- SD card (very large volumes, removable storage)
- External EEPROM / FRAM

**Behavior:**
- The persist layer is the primary buffer. Its size determines how many events the SDK can hold between transmissions.
- When the persist layer is full and a new event arrives, the oldest event is dropped and the module-level drop counter is incremented.
- The persist layer does not interpret event contents. It is a dumb byte store — it appends bytes, reads bytes back in order, and discards bytes from the front.
- The SDK owns event framing (2-byte length prefixes). The persist PAL only needs to handle raw byte sequences.

**PAL interface:**

```c
/* Append raw bytes to the end of storage.
 * The SDK calls this with length-prefixed CBOR events.
 * The persist implementation does not need to understand the format —
 * just store the bytes and return them in the same order via peek.
 * Returns 0 on success, -1 on error. */
int dtf_pal_persist_append(const uint8_t* data, size_t len);

/* Read up to buf_len bytes from the front of storage without consuming them.
 * Sets *out_len to actual bytes read.
 * Data remains in storage until dtf_pal_persist_discard() is called.
 * This two-phase peek/discard pattern ensures events are not lost
 * if transmission fails — they remain available for retry.
 * Returns 0 on success, -1 on error. */
int dtf_pal_persist_peek(uint8_t* buf, size_t buf_len, size_t* out_len);

/* Remove len bytes from the front of storage.
 * Called by the SDK after successful transmission to free space.
 * The len value always corresponds to complete events (the SDK
 * calculates exact byte boundaries using the length prefixes it wrote).
 * Returns 0 on success, -1 on error. */
int dtf_pal_persist_discard(size_t len);

/* Returns the number of bytes currently stored. */
size_t dtf_pal_persist_available(void);
```

**Why peek/discard instead of read-and-delete:** If the transmit stage fails (network error, timeout), the events are still in the persist layer and will be retried on the next send cycle. Events are only removed after the transport confirms delivery. The persist implementor doesn't need to track cursors or manage ack state — the SDK tells it exactly how many bytes to discard.

**Example: implementing a LittleFS persist backend:**
```c
// append → lfs_file_write at end of file
// peek   → lfs_file_read from current read offset (do not advance permanently)
// discard → advance read offset, compact file when fragmentation exceeds threshold
// available → file size minus read offset
```

### 2.3 Transmit

**Responsibility:** Read persisted events, wrap them in a CBOR wire-format envelope, and deliver them to the remote endpoint.

**Default implementation:** HTTPS POST with `Content-Type: application/cbor` and Bearer token authentication.

**Alternative implementations (user-provided via PAL):**
- MQTT publish
- UDP (for lossy-tolerant, high-frequency metrics)
- Cellular (LTE-M / NB-IoT AT commands)
- Satellite (store-and-forward protocols)

**Wire format:**

The entire payload is CBOR (RFC 8949), front to back. No JSON anywhere in the pipeline. The `"src"` field is always `"device"` for payloads originating from the SDK, distinguishing them from payloads submitted by external tools such as serial log capture applications.

```
CBOR map(5 or 6) {
  text("src"):     text("device"),           // always "device" — distinguishes from serial logger
  text("d"):       text(device_id),
  text("fv"):      text(fw_version),
  text("hw"):      text(hw_variant),
  text("dropped"): uint(N),                  // present only when N > 0
  text("e"):       array(event_count) [
                     <raw CBOR event bytes>,  // copied verbatim from persist
                     <raw CBOR event bytes>,
                     ...
                   ]
}
```

Log event (encoded at ingest, stored as-is in persist):
```
CBOR map(6) {
  text("t"):   text("l"),
  text("bsn"): uint(boot_sequence_number),
  text("up"):  uint(uptime_ms),
  text("lv"):  text("d"|"i"|"w"|"e"),
  text("mod"): text(module_name),
  text("m"):   text(message)
}
```

Metric event:
```
CBOR map(5) {
  text("t"):   text("m"),
  text("bsn"): uint(boot_sequence_number),
  text("up"):  uint(uptime_ms),
  text("n"):   text(metric_name),
  text("v"):   int(value)
}
```

**Chunked transmit behavior:**

The transmit stage sends events in chunks sized to fit the TX buffer. It does not attempt to send the entire persist buffer in one request.

```
1. peek up to TX_BUFFER_SIZE bytes from persist
2. walk the 2-byte length-prefixed events:
   - read length prefix
   - if this event fits in remaining TX space (after envelope overhead), include it
   - if not, stop — this is the chunk boundary
3. build CBOR envelope: device metadata + drop count + array of included events
   (event CBOR bytes are copied verbatim from the peek buffer, length prefixes stripped)
4. call dtf_pal_transport_send() with the complete CBOR payload
5. on success: dtf_pal_persist_discard(bytes consumed from persist)
   on failure: do nothing — events remain for retry
```

If more events remain after a successful chunk, managed mode will send the next chunk on the next timer cycle. In manual mode, the user calls `dtf_send()` again (see section 3.3).

**PAL interface (existing, unchanged):**

```c
/* Send a complete CBOR payload to the ingest endpoint.
 * The payload is fully formed — the transport just delivers it.
 * Returns 0 on success, non-zero on error. */
int dtf_pal_transport_send(const char* url,
                           const char* auth_header,
                           const uint8_t* payload,
                           size_t len);
```

## 3. Pipeline execution

### 3.1 Two operations

The pipeline performs two distinct operations that happen at different rates:

| Operation | What it does | Speed | Cost |
|-----------|-------------|-------|------|
| **Process** | Drains ingest queue into persist layer | Fast (memory or local I/O) | Negligible |
| **Send** | Reads from persist, wraps envelope, transmits one chunk | Slow (network) | Power, bandwidth |

These are exposed as two user-facing functions for manual mode, and handled automatically in managed mode.

### 3.2 Managed mode (default)

A background FreeRTOS task runs the pipeline automatically:

```
loop:
    dtf_process()           // drain ingest → persist (every iteration)
    if threshold_reached() and cooldown_elapsed():
        dtf_send()          // persist → transmit (one chunk)
    else if timer_elapsed():
        dtf_send()          // periodic heartbeat flush
    vTaskDelay(...)
```

**Flush triggers:**
- **Capacity trigger:** persist layer reaches 75% full and cooldown has elapsed.
- **Periodic timer:** fires every `DTF_OBS_FLUSH_MIN_INTERVAL_MS` regardless of fill level, ensuring quiet devices still report.
- **Cooldown:** after any transmit, the next automatic transmit is suppressed until the minimum interval elapses. Prevents flush storms under heavy logging. The cooldown applies only to managed mode — it does not affect manual `dtf_send()` calls.

The user enables managed mode by leaving `DTF_OBS_MANUAL_MODE` disabled (the default). They never call `dtf_process()` or `dtf_send()` — the SDK handles everything.

### 3.3 Manual mode

The user disables the background task and drives the pipeline themselves:

```c
void   dtf_process(void);
int    dtf_send(void);
size_t dtf_pending(void);
```

**`dtf_process()`**
- Drains all pending events from the ingest queue into the persist layer.
- Fast. No network I/O. No blocking beyond a brief mutex acquisition.
- Always safe to call. Idempotent — calling with an empty ingest queue is a no-op.
- Call this as often as practical to minimize the window where events could be dropped from the ingest queue.

**`dtf_send()`**
- Calls `dtf_process()` internally first (so the user doesn't have to remember).
- Reads one TX-buffer-sized chunk from the persist layer, builds the CBOR envelope, transmits.
- No cooldown. When the user calls this, they want to send now.
- Returns: 0 = chunk sent successfully, 1 = nothing to send (persist is empty), -1 = transport error.
- Thread-safe. May block for up to the transport timeout (default 10s).

**`dtf_pending()`**
- Returns the number of bytes currently in the persist layer awaiting transmission.
- Use this to drive drain loops or to decide whether it's worth connecting to the network.

**Usage patterns:**

Ultra-low-power sensor (wake, measure, sleep):
```c
void app_main(void) {
    dtf_init(&config);

    while (1) {
        read_sensors();
        dtf_metric("temp", temperature);
        dtf_metric("humidity", humidity);
        dtf_log(DTF_LOG_DEBUG, "sensor", "reading complete");
        dtf_process();  // persist immediately before sleeping

        if (time_to_report()) {
            connect_wifi();
            while (dtf_pending() > 0) {
                if (dtf_send() < 0) break;  // stop on transport error
            }
            disconnect_wifi();
        }

        deep_sleep_minutes(5);
    }
}
```

Main-loop driven (no background task, periodic connectivity):
```c
void app_main(void) {
    config.manual_mode = true;
    dtf_init(&config);

    while (1) {
        app_do_work();       // may call dtf_log/dtf_metric internally
        dtf_process();       // persist queued events

        if (wifi_connected()) {
            dtf_send();      // send one chunk; next chunk on next iteration
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

Default managed mode (most users):
```c
void app_main(void) {
    dtf_config_t config = DTF_CONFIG_DEFAULT();
    config.api_key = "dtf_k_...";
    dtf_init(&config);
    // done — observability enabled, device ID from MAC, hw from chip,
    // background task handles everything
}
```

## 4. Memory model

### 4.1 Zero heap allocation

In the default configuration, the SDK performs no heap allocation. All memory is statically allocated at compile time based on Kconfig values. This eliminates fragmentation, makes memory usage deterministic, and avoids heap contention with the application.

### 4.2 Memory regions

| Region | Purpose | Size | Lifetime |
|--------|---------|------|----------|
| Ingest queue | Small RAM buffer for incoming events before persist | Fixed, internal (not user-configurable) | Permanent |
| Persist buffer | Primary event storage (default: RAM byte ring) | `DTF_OBS_PERSIST_BUFFER_SIZE` bytes | Permanent |
| TX buffer | Workspace for building wire-format payloads | `DTF_OBS_TX_BUFFER_SIZE` bytes | Permanent |
| Auth header | Pre-built "Bearer ..." string | Computed from API key length at init | Permanent |

### 4.3 User-provided allocation

Users who prefer dynamic allocation can override the PAL allocator:

```c
void* dtf_pal_alloc(size_t size);
void  dtf_pal_free(void* ptr);
```

The default (weak-linked) implementation returns pointers into the static buffers. A user override could use `malloc`, PSRAM allocation, or any custom allocator. The SDK calls these only during `dtf_init()` — there are no runtime allocations after initialization.

### 4.4 RAM budget calculation

The two primary knobs are `DTF_OBS_PERSIST_BUFFER_SIZE` and `DTF_OBS_TX_BUFFER_SIZE`. The total SDK memory footprint is approximately:

```
Total ≈ DTF_OBS_PERSIST_BUFFER_SIZE + DTF_OBS_TX_BUFFER_SIZE + ingest queue + auth header
```

The ingest queue and auth header are small fixed costs (typically < 1 KB combined).

The persist buffer holds CBOR-encoded events of variable size:

| Event type | Typical encoded size | Maximum encoded size |
|------------|---------------------|---------------------|
| Metric | 20-30 bytes | ~(25 + METRIC_NAME_MAX_LEN) bytes |
| Log (short message) | 40-60 bytes | ~(50 + MOD_MAX_LEN + MSG_MAX_LEN) bytes |
| Log (max message) | 150-200 bytes | ~(50 + MOD_MAX_LEN + MSG_MAX_LEN) bytes |

Examples at default string limits (MSG_MAX_LEN=128, MOD_MAX_LEN=32):

| Persist buffer | Approx. capacity (all metrics) | Approx. capacity (all max-length logs) | Approx. capacity (mixed typical) |
|----------------|-------------------------------|---------------------------------------|----------------------------------|
| 4096 bytes | ~160 metrics | ~22 logs | ~40-80 events |
| 8192 bytes | ~320 metrics | ~45 logs | ~80-160 events |
| 16384 bytes | ~640 metrics | ~90 logs | ~160-320 events |

The TX buffer controls how many events are sent per chunk. A larger TX buffer means fewer HTTP requests to drain the persist buffer. A smaller TX buffer uses less RAM but requires more send cycles. The TX buffer does not need to hold the entire persist buffer — the SDK chunks automatically.

## 5. Event drop handling

### 5.1 Unified drop counter

A single module-level drop counter tracks all lost events regardless of where they were dropped:

- **Ingest queue overflow:** The ingest queue is small and drains quickly (in managed mode, nearly continuously). Drops here indicate the application is producing events faster than the persist layer can absorb them. This should be rare. Each drop increments the unified counter.

- **Persist layer overflow:** The persist buffer is full and cannot be drained because transmit hasn't happened (no connectivity, manual mode and user hasn't called `dtf_send()`). This is the expected drop point under heavy load or infrequent connectivity. Each drop increments the unified counter.

The counter is reset to zero after each successful transmission. This means the `"dropped"` field in each payload represents the number of events lost since the last successful delivery.

### 5.2 Drop visibility

**Local (device-side):**
- Each drop emits a stderr warning: `[dtf_obs] event dropped (N total since last send) — increase DTF_OBS_PERSIST_BUFFER_SIZE or send more frequently`
- The warning is rate-limited to avoid spamming the console during bursts. The first drop warns immediately; subsequent drops update the count but don't emit until the count crosses a threshold (10, 100, etc.) or a send completes.

**Remote (backend-side):**
- Each transmitted payload includes a `"dropped"` field with the count of events lost since the last successful transmission. Omitted when zero for wire efficiency.
- The backend can alert, dashboard, or aggregate this metric to identify devices that need configuration tuning.

### 5.3 Drop policy

Drops always discard the **oldest** event to make room for the newest. This is intentional — in most embedded scenarios, the most recent data is most valuable. The dropped event's content is lost, but its existence is counted in the unified drop counter.

## 6. Configuration reference

### 6.1 Kconfig options

All options live under `menuconfig → Deploy The Fleet SDK → Observability`.

#### Kconfig dependency tree

```
DTF_OBSERVABILITY                    (master toggle)
├── DTF_OBS_PERSIST_BUFFER_SIZE      (always visible)
├── DTF_OBS_TX_BUFFER_SIZE           (always visible)
├── DTF_OBS_MANUAL_MODE              (always visible)
├── DTF_OBS_FLUSH_MIN_INTERVAL_MS    (depends on !DTF_OBS_MANUAL_MODE)
├── DTF_OBS_GATEWAY_URL              (always visible)
├── DTF_OBS_DEVICE_ID                (always visible)
├── DTF_OBS_HW_VARIANT               (always visible)
├── DTF_OBS_MSG_MAX_LEN              (always visible)
├── DTF_OBS_MOD_MAX_LEN              (always visible)
└── DTF_OBS_METRIC_NAME_MAX_LEN      (always visible)
```

The flush interval only appears in menuconfig when manual mode is off — it has no meaning when the user drives the pipeline themselves. All other settings are always visible under the Observability menu.

Note: `api_key` is intentionally **not** a Kconfig option. API keys are secrets and Kconfig values end up in `sdkconfig` files which are often committed to version control. The API key must be provided at runtime via `dtf_config_t.api_key`.

#### Core settings

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `DTF_OBSERVABILITY` | bool | n | Master enable. When disabled, all observability code is excluded at compile time — zero binary size impact. |
| `DTF_OBS_PERSIST_BUFFER_SIZE` | int | 8192 | Total bytes allocated for the persist buffer (default RAM byte ring). This is the primary memory budget knob. |
| `DTF_OBS_TX_BUFFER_SIZE` | int | 2048 | Size of the transmit buffer in bytes. Controls how many events are sent per network request. Larger values mean fewer requests but more RAM usage. |
| `DTF_OBS_FLUSH_MIN_INTERVAL_MS` | int | 10000 | Minimum time between automatic network transmissions (cooldown). Also serves as the periodic flush timer in managed mode. Only visible when `DTF_OBS_MANUAL_MODE` is disabled. |
| `DTF_OBS_GATEWAY_URL` | string | `https://ingest.deploythefleet.com/v1/ingest` | Ingest endpoint URL. Override for staging, self-hosted, or development tunnels. |
| `DTF_OBS_MANUAL_MODE` | bool | n | When enabled, no background task is created. The user drives the pipeline via `dtf_process()` and `dtf_send()`. |

#### Device identity

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `DTF_OBS_DEVICE_ID` | string | `""` (empty) | Device identifier. When empty, the SDK derives a default from the device's MAC address (e.g., `"aabbccddeeff"`). Override for fleets with custom provisioning schemes. |
| `DTF_OBS_HW_VARIANT` | string | `""` (empty) | Hardware variant label. When empty, the SDK derives a default from `esp_chip_info()` (e.g., `"esp32s3"`, `"esp32c3"`). Override for custom hardware (e.g., `"sensor_board_v2"`). |

Firmware version is not a Kconfig option — the SDK derives it automatically via `dtf_pal_get_fw_version()`, which defaults to `esp_app_get_description()->version` (set by `PROJECT_VER` in the project's root `CMakeLists.txt`). Like the other identity fields, it can be overridden at runtime via `dtf_config_t.fw_version`.

#### Resolution order for identity fields

Each identity field follows the same priority:

1. **`dtf_config_t` field at runtime** (highest priority) — for dynamic provisioning
2. **Kconfig value** (if non-empty) — for build-time configuration
3. **SDK-derived default** (lowest priority) — for zero-config operation

This means a zero-config user gets sensible values automatically (MAC address, chip type, project version). A user with a fleet provisioning system overrides device ID at runtime. A user with custom hardware sets `DTF_OBS_HW_VARIANT` in menuconfig.

The SDK-derived defaults are provided by PAL functions `dtf_pal_get_device_id()` and `dtf_pal_get_hw_variant()` in `dtf_pal_esp32.c`. The platform-agnostic core only sees strings. These PAL functions can be overridden with strong definitions if the default derivation logic doesn't suit a platform.

#### String limits

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `DTF_OBS_MSG_MAX_LEN` | int | 128 | Maximum log message length in characters. Longer messages are truncated at ingest time. Directly affects per-event encoded size and therefore how many events fit in the persist buffer. |
| `DTF_OBS_MOD_MAX_LEN` | int | 32 | Maximum module/subsystem name length. |
| `DTF_OBS_METRIC_NAME_MAX_LEN` | int | 16 | Maximum metric name length. |

#### Kconfig help text examples

```
config DTF_OBS_PERSIST_BUFFER_SIZE
    int "Persist buffer size (bytes)"
    depends on DTF_OBSERVABILITY
    default 8192
    help
        Total memory allocated for buffering observability events.
        This is the primary memory budget for the observability
        subsystem. Events are CBOR-encoded and stored compactly:

        - Metric events typically use 20-30 bytes each
        - Log events use 40-200 bytes depending on message length

        Examples at default message length (128 chars):
            4096 bytes  ≈ 22-160 events depending on mix
            8192 bytes  ≈ 45-320 events depending on mix
            16384 bytes ≈ 90-640 events depending on mix

        If events are being dropped (visible in device logs and
        in the "dropped" field of telemetry payloads), either
        increase this value or decrease
        DTF_OBS_FLUSH_MIN_INTERVAL_MS.

config DTF_OBS_TX_BUFFER_SIZE
    int "Transmit buffer size (bytes)"
    depends on DTF_OBSERVABILITY
    default 2048
    help
        Size of the buffer used to build outgoing CBOR payloads.
        The SDK automatically chunks transmissions to fit within
        this buffer. Larger values send more events per request
        (fewer HTTP round trips) but use more RAM.

        Minimum recommended: 512 bytes (enough for a few events).
        The buffer must be large enough to hold the CBOR envelope
        overhead (~100 bytes) plus at least one maximum-sized event.

config DTF_OBS_FLUSH_MIN_INTERVAL_MS
    int "Minimum flush interval (ms)"
    depends on DTF_OBSERVABILITY
    depends on !DTF_OBS_MANUAL_MODE
    default 10000
    help
        Minimum time between automatic network transmissions in
        managed mode. Also serves as the periodic heartbeat timer —
        even if the persist buffer has not reached the flush
        threshold, a send will occur at this interval.

        Lower values: more responsive, more network usage.
        Higher values: less network usage, more buffering needed.

        This setting has no effect in manual mode. When calling
        dtf_send() manually, there is no cooldown — each call
        sends immediately if data is available.

config DTF_OBS_DEVICE_ID
    string "Device ID"
    depends on DTF_OBSERVABILITY
    default ""
    help
        Unique device identifier sent with every telemetry payload.
        When empty (default), the SDK automatically uses the
        device's MAC address (e.g., "aabbccddeeff").

        Override this for fleets with custom provisioning schemes
        or when a human-readable device name is preferred.

        Can also be set at runtime via dtf_config_t.device_id,
        which takes priority over this Kconfig value.

config DTF_OBS_HW_VARIANT
    string "Hardware variant"
    depends on DTF_OBSERVABILITY
    default ""
    help
        Hardware variant label sent with every telemetry payload.
        When empty (default), the SDK automatically uses the chip
        type from esp_chip_info() (e.g., "esp32s3", "esp32c3").

        Override this for custom hardware boards (e.g.,
        "sensor_board_v2", "gateway_r3").

        Can also be set at runtime via dtf_config_t.hw_variant,
        which takes priority over this Kconfig value.
```

### 6.2 Runtime configuration (dtf_config_t)

All Kconfig values can be overridden at runtime via `dtf_config_t` fields passed to `dtf_init()`. Runtime values take priority over Kconfig defaults. This allows a single firmware binary to behave differently based on provisioned configuration.

The `api_key` field is runtime-only — it has no Kconfig equivalent. The `device_id`, `hw_variant`, and `fw_version` fields follow the three-level resolution order described above (runtime → Kconfig → SDK default).

### 6.3 PAL overrides

PAL functions are overridable in two ways, depending on the use case:

**Config callbacks (`dtf_config_t`)** — for functions with legitimate runtime-swap use cases (test stubs, per-deployment transport selection, custom BSN storage based on provisioned config). Set the callback in the config struct before calling `dtf_init()`. NULL uses the weak-linked default.

| Config field | PAL default | Override for |
|---|---|---|
| `transport_send` | HTTPS POST with `application/cbor` | MQTT, UDP, cellular, satellite, test stubs |
| `read_bsn` | NVS namespace "dtf" | Custom BSN persistence |
| `write_bsn` | NVS namespace "dtf" | Custom BSN persistence |
| `persist_append` | RAM byte ring | LittleFS, SD, NVS, FRAM, spillover logic *(future — not yet implemented)* |
| `persist_peek` | RAM byte ring | LittleFS, SD, NVS, FRAM, spillover logic *(future)* |
| `persist_discard` | RAM byte ring | LittleFS, SD, NVS, FRAM, spillover logic *(future)* |
| `persist_available` | RAM byte ring | LittleFS, SD, NVS, FRAM, spillover logic *(future)* |

**Weak-linked PAL functions** — for platform decisions made at compile time. Provide a strong (non-weak) definition of the function anywhere in the application.

*Allocation (future — not yet implemented in v1):*

| PAL function | Default | Override for |
|---|---|---|
| `dtf_pal_alloc` | Static buffer | malloc, PSRAM, custom allocator |
| `dtf_pal_free` | No-op | free, PSRAM, custom allocator |

*Platform services:*

| PAL function | Default | Override for |
|---|---|---|
| `dtf_pal_clock_uptime_ms` | `esp_timer_get_time()` | Custom clock source |
| `dtf_pal_timer_create` | FreeRTOS task (8 KiB stack) | Custom scheduler |
| `dtf_pal_mutex_create` | FreeRTOS semaphore | Custom concurrency |
| `dtf_pal_mutex_lock` | FreeRTOS semaphore | Custom concurrency |
| `dtf_pal_mutex_unlock` | FreeRTOS semaphore | Custom concurrency |

*Device identity:*

| PAL function | Default | Override for |
|---|---|---|
| `dtf_pal_get_device_id` | WiFi STA MAC address | Custom device ID derivation |
| `dtf_pal_get_hw_variant` | `esp_chip_info()` chip type | Custom hardware variant string |
| `dtf_pal_get_fw_version` | `esp_app_get_description()->version` | Custom firmware version string |

## 7. Public API summary

The public API is the only surface application code interacts with. All pipeline internals, PAL calls, framing, serialization, and chunking are hidden behind these functions.

### Initialization

```c
/* Initialize the DTF SDK. Call once at startup.
 * Returns 0 on success, -1 on error. */
int dtf_init(const dtf_config_t* config);
```

### Event ingestion

```c
/* Log a message. Level, module, and message are captured immediately.
 * Never blocks. Safe to call from any task context. */
void dtf_log(dtf_log_level_t level, const char* module, const char* message);

/* Record a metric. Name and value are captured immediately.
 * Never blocks. Safe to call from any task context. */
void dtf_metric(const char* name, int32_t value);
```

### Pipeline control

```c
/* Drain the ingest queue into the persist layer.
 * Fast, no network I/O, always safe, idempotent.
 * In managed mode, this is called automatically. */
void dtf_process(void);

/* Persist pending events, then transmit one chunk to the remote endpoint.
 * Calls dtf_process() internally.
 * No cooldown — each call sends immediately if data is available.
 * Returns: 0 = chunk sent, 1 = nothing to send, -1 = transport error.
 * May block for up to the transport timeout. */
int dtf_send(void);

/* Returns the number of bytes pending in the persist layer.
 * Use this to decide whether to connect to the network or
 * to drive a drain loop with dtf_send(). */
size_t dtf_pending(void);
```

## 8. Byte ring internals (default persist backend)

This section describes the default RAM persist implementation. Users who provide a custom persist backend do not need to understand this. Users who only use the public API (`dtf_log`, `dtf_metric`, `dtf_send`) do not need to understand this.

### 8.1 Structure

The byte ring is a circular buffer of raw bytes with per-event framing:

```
[ len_hi | len_lo | cbor_event_bytes... | len_hi | len_lo | cbor_event_bytes... | ... ]
```

Each event is prefixed with a 2-byte big-endian length (max event size: 65535 bytes). This allows the ring to:

- Store variable-length events with no padding waste
- Drop the oldest event by reading its length prefix and advancing the tail pointer
- Walk events sequentially for reading without parsing CBOR structure

The framing is written by the SDK at ingest time, not by the persist PAL. A custom persist backend only sees raw byte sequences via `append`/`peek`/`discard` — it does not need to understand the framing format.

### 8.2 Operations

**Append:** The SDK CBOR-encodes the event, computes its length, and calls `dtf_pal_persist_append()` with the 2-byte length prefix followed by the CBOR bytes. If insufficient space is available, the SDK reads the oldest event's length prefix via `peek`, calls `discard` to remove it, and increments the drop counter. This repeats until enough space is available.

**Peek:** The SDK calls `dtf_pal_persist_peek()` to read bytes from the front of storage without consuming them. It then walks the 2-byte length prefixes to identify event boundaries and determine how many complete events fit in the TX buffer.

**Discard:** After successful transmission, the SDK calls `dtf_pal_persist_discard()` with the exact number of bytes that were transmitted (sum of length prefixes + event bodies for all events in the chunk). The persist backend removes those bytes from the front of storage.

### 8.3 Wraparound (RAM byte ring default)

In the default RAM implementation, events may wrap around the end of the circular buffer. The length prefix and event body are written/read with modular arithmetic on the buffer indices. An event is never split across the wrap boundary — if insufficient contiguous space exists at the end, the remaining bytes are skipped and the event is written at the beginning (a small amount of waste, bounded by the maximum event size, and only at the wrap point).

## 9. Serialization format

### 9.1 CBOR encoding

Events are encoded in CBOR (RFC 8949) at ingest time. CBOR is used over JSON for:

- **Compact binary representation.** Integers encode in 1-5 bytes instead of variable-length decimal strings. No field delimiters, no escaping.
- **Zero-copy friendly.** Encoded events stored in the persist layer are copied verbatim into the transmit envelope without re-serialization.
- **Deterministic sizing.** The maximum encoded size of an event is computable at compile time from the string length limits.

### 9.2 Content-Type

The default transport sets `Content-Type: application/cbor` on all requests. The entire payload — envelope and events — is pure CBOR.

### 9.3 Encoder implementation

The CBOR encoder is hand-rolled (~70 lines) with no external library dependency. It supports only the types needed by this SDK: unsigned integers, negative integers, text strings, maps, and arrays. This avoids adding a library dependency for a small, fixed-format encoding task.

## 10. Future extensions

### 10.1 ESP-IDF log hook

Capture `ESP_LOGx()` output at the configured log level by registering a custom `esp_log_set_vprintf` handler. Captured log lines are parsed into level/module/message and fed into `dtf_log()`. This makes the SDK transparent — existing application code gets observability without modification.

### 10.2 Structured events

A `dtf_event()` API for application-defined structured events with custom key-value fields. These flow through the same pipeline but carry richer schemas.

### 10.3 ISR-safe ingest

Allow `dtf_log()` and `dtf_metric()` to be called from interrupt context. Requires a lock-free ingest queue (ring buffer with atomic head/tail updates). The persist drain would remain task-context only.

### 10.4 Payload compression

For bandwidth-constrained transports (cellular, satellite), optionally compress the CBOR payload before transmission. CBOR's binary format already compresses well with simple schemes like LZ4 or heatshrink.

### 10.5 Batch acknowledgment from backend

The backend returns a sequence number in the HTTP response indicating the latest batch received. The SDK uses this to handle retries more precisely — avoiding duplicate delivery without requiring exactly-once semantics.
