# HTTP and MQTT interface, version 1

All temperatures are degrees C. Coordinates are integer native sensor pixels, origin top-left: x 0–79, y 0–61. ROI extents are exclusive on the right and bottom (`x + width <= 80`, `y + height <= 62`). Detection is based on temperature difference, independent of the display palette.

## HTTP

| Method and path | Response |
| --- | --- |
| `GET /` | Thermal dashboard with small/large detection panels, full-screen control and settings link; no external assets |
| `GET /settings` | Configuration page with video, draggable ROI, compact detection panels and diagnostics |
| WebSocket `/stream.yuy2` | Binary 80 x 62 YUY2 with per-frame metadata; maximum two clients |
| `GET /video.js` | Embedded browser decoder and overlay renderer |
| `GET /snapshot.bmp` | Uncompressed 80 x 62 grayscale BMP, no overlays; 503 when paused or no healthy frame younger than one second is available |
| `GET /api/config` | Persisted settings, with password-presence flags instead of passwords |
| `PUT /api/config` | Partial settings update; persist atomically before applying; 400 for invalid settings |
| `GET /api/status` | State, temperatures, sensor age, capture FPS, memory and connection health |
| `POST /api/relearn` | Restart learning on the next acquisition iteration |

Firmware 0.3.0 replaces MJPEG with YUY2 and removes `/stream.mjpg` and `/snapshot.jpg` (404). Configuration and MQTT schemas are unchanged; new devices default to White hot and existing saved palettes are preserved. Both pages read the existing debounced `small.state` and `large.state`, polling status with a 750 ms delay between completed requests and a single request in flight. Failed responses, stale sensor frames or a 2.5-second response watchdog make the panels unavailable. Video can be disabled without disabling detection indicators. Region measurements are shown only for an occupied channel with a current nonzero pixel area; a retained occupied state during the clear delay does not display old measurements.

Write requests must contain `X-Thermal-Request: 1`; this prevents simple cross-origin form submissions, not access by other local clients. The server sends no permissive CORS headers. `PUT` accepts a 2–4096 byte JSON object. Unknown keys are ignored. An omitted password is preserved; an explicit empty string clears it. Wi-Fi passwords must be empty or 8–63 characters. Broker accepts hostname/IPv4, not URI/IPv6. Empty broker disables MQTT. The topic prefix must be nonempty, not start with `$`, contain no `+` or `#`, and have no trailing slash.

Example partial update:

```http
PUT /api/config HTTP/1.1
Content-Type: application/json
X-Thermal-Request: 1

{"roi":{"x":10,"y":12,"width":50,"height":40},"delta_c":3,"min_pixels":4,"activate_ms":500,"clear_ms":2000,"learn_ms":10000}
```

Other configuration fields: `ssid`, `wifi_password`, `broker`, `port`, `mqtt_user`, `mqtt_password`, `topic`. GET adds `version: 1`, `has_wifi_password` and `has_mqtt_password`. PUT success is `{"saved":true,"background_learning_restarted":true}`. Relearn success is `{"learning":true}`. Errors contain `{"error":"..."}`. A successful save also reconnects MQTT and restarts learning; a Wi-Fi change can interrupt the response.

Firmware 0.1.3 adds the saved boolean `flip_horizontal` (default false). `PUT /api/config` with `{"flip_horizontal":true}` flips all rendered outputs horizontally. GET config and status report the flag. ROI and detection bounds in HTTP/MQTT always use native sensor coordinates; the browser transforms drawing and pointer coordinates. Non-boolean flag values are rejected. Status also adds `mqtt_configured`, so clients can distinguish disabled MQTT from a disconnected configured broker.

Firmware 0.2.0 adds these saved settings (old configurations use the defaults):

| Field | Default | Accepted values |
| --- | --- | --- |
| `palette` | `white_hot` | `fire`, `ironbow`, `rainbow`, `white_hot`, `black_hot`; colours are browser-side, USB/BMP use grayscale |
| `large_min_pixels` | `16` | Integer 1–4960; connected-region areas at or above this value are large |
| `ha_discovery` | `true` | JSON boolean; enables Home Assistant discovery on the configured broker |

`min_pixels` still rejects small noise regions from every channel. Qualifying regions below `large_min_pixels` belong to the small channel; those at/above it belong to the large channel. Each channel uses the same configured delays independently. If the cutoff is at/below `min_pixels`, no regions qualify as small; if above ROI area, no regions qualify as large. Multiple regions are classified individually, with touching/overlapping regions forming one eight-connected component. Classifying by pixel area is not physical-size estimation.

Status adds `palette`, plus `small` and `large` objects containing `state`, `pixels`, `peak_c` and `bounds`. `pixels`/`bounds` refer to the largest current component in that class; `peak_c` is the maximum among its current qualifying pixels, or null when no matching region is present or the channel is learning/unavailable. During the clear delay a channel can still be occupied with zero pixels and null peak. Both channels may be occupied simultaneously, including briefly while a region changes size class.

`delta_c`: 0.2–100; `min_pixels`: 1 through ROI area; activation/clear delay: 0–60000 ms; learning: 1000–120000 ms. Invalid or partial-out-of-image ROIs are rejected, not clipped silently.

Status includes the fields of `/state` below, plus `fps` (acquisition, not USB delivery), `frames`, `frame_age_ms`, `sensor_ready`, `sensor_error`, `sensor_errors`, `min_c`, `max_c`, `free_heap`, `free_psram`, `free_internal_heap`, `min_internal_heap`, `largest_internal_block`, `wifi_connected`, `mqtt_connected`, `usb`, `test_pattern`, `ip`, `firmware`, `arduino`, `idf`, and `reset_reason` (the numeric ESP-IDF reset-reason enum). Temperatures require fresh frames and completion of `sensor_warmup_ms`; startup values can drift substantially. Availability and detection state have different meanings: a reachable device can have an unavailable sensor.

Firmware 0.2.1 adds `sensor_recoveries` (capture restart attempts), `sensor_last_error_code` (last vendor bitmask; decimal 4 means data-available timeout), `sensor_recovering`, and `sensor_config_pauses` (configuration-save pauses). Counters persist until the ESP32 reboots; they do not mean the current frame is faulty. Saving settings briefly pauses capture before writing NVS, then discards partial frames and relearns the scene. Sensor errors immediately mark aggregate/small/large states unavailable; recovery returns them to learning without replaying previous transitions. A save during sensor initialization can return an error asking the caller to retry; no settings are written without a pause acknowledgement.

Video diagnostics in 0.3.0: `video_fps` (generated YUY2 frames per second), `video_frames` (total generated frames), `video_format` (`YUY2`), `video_width` (80), `video_height` (62), `video_target_fps` (20), `frame_bytes` (9920), `render_us` (latest conversion duration) and `render_ms` (the same duration in milliseconds). `jpeg_bytes` is removed. These distinguish sensor acquisition from conversion; USB/browser delivery and end-to-end lag must still be measured at the receiving host.

Firmware 0.1.2 adds `sensor_warmup_ms`, the remaining startup hold time. While positive, valid sensor frames can be displayed (`sensor_ready=true`), but detection remains `learning` and emits no occupied/clear transitions. Temperatures during this hold are unsettled. Background learning starts after the hold ends. The default hold is 120 seconds after the first valid frame; synthetic mode skips it. This duration is an initial safeguard based on observed drift, not a manufacturer calibration guarantee.

WebSocket requests receive an empty binary message when no new healthy frame is available. The browser reconnects after 1.5 seconds without progress or a reply; status polling separately controls sensor/pause availability. A failed sensor does not keep serving old snapshots. A USB host may freeze its last frame; check `/api/status` or MQTT rather than inferring health from the displayed USB picture.

## YUY2 video protocol (0.3.0)

USB advertises one uncompressed YUY2 format: 80 x 62, 16 bits/pixel, 20 FPS (500,000 units of 100 ns), 9,920 bytes per image. Pixels are row-major packed `Y0 U Y1 V`; Y is limited range 16–235 and U/V are always 128. White hot is used unless the saved palette is Black hot. Flip is already applied to pixels. No metadata, bounding boxes, borders or state stripes are included in the USB image.

The browser opens `/stream.yuy2` as a WebSocket and sends a binary message containing the single byte `1` for each next frame. Keep one request in flight, paced at up to 20 per second. The response is one 9,984-byte binary message, or an empty binary message if there is no newer valid frame. Each connection remembers the last serial it sent. Malformed requests close the connection; a third simultaneous stream is closed. Disconnect frees the session buffer. Slow clients receive the newest frame on their next request; no frame history is queued.

The response has a 64-byte header followed by the same 9,920 image bytes used by USB. All multibyte fields are little-endian; floats are IEEE-754 binary32. Reserved bytes are zero.

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | ASCII `YUY2` |
| 4, 6 | uint16 each | Protocol version 1; header length 64 |
| 8, 10 | uint16 each | Width 80; height 62 |
| 12, 16 | uint32 each | Sensor frame serial; capture uptime in milliseconds (wraps) |
| 20 | uint8 | Bit 0: horizontally flipped |
| 21 | uint8 | Palette: Fire 0, Ironbow 1, Rainbow 2, White hot 3, Black hot 4 |
| 22, 23 | uint8 each | State: unavailable 0, learning 1, clear 2, occupied 3; reserved |
| 24, 28 | float32 each | Frame minimum and maximum Celsius |
| 32, 40, 48 | four uint16 each | ROI, small bounds, large bounds: x, y, width, height |
| 56, 58 | uint16 each | Current small and large region pixel counts |
| 60 | 4 bytes | Reserved |
| 64 | 9,920 bytes | Packed YUY2 pixels |

Bounds stay in native sensor coordinates: mirror x as `80 - x - width` when the flip flag is set. Draw a class box only when its count and extents are nonzero. This keeps overlays synchronized with their image, independently of the slower occupancy/status poll. The browser expands Y to 0–255 and applies the selected palette locally (Black hot is already inverted in the pixels). ROI editing uses a separate canvas at the same 80:62 aspect ratio. Snapshots are 6,038-byte uncompressed top-down 8-bit grayscale BMPs with a 256-entry grayscale palette.

## MQTT

All messages are QoS 1. The default base topic is `thermal/<12-digit-station-MAC>`. Availability is a retained plain string, `online` after connection and `offline` through the broker last will (30-second keepalive) on unexpected loss. Broker detection of loss is not immediate.

Retained `/state` example:

```json
{"version":1,"device_id":"aabbccddeeff","roi":"roi-1","state":"occupied","uptime_ms":23800,"peak_c":34.2,"pixels":12,"bounds":{"x":20,"y":18,"width":4,"height":3}}
```

Non-retained `/event` example:

```json
{"version":1,"device_id":"aabbccddeeff","roi":"roi-1","state":"occupied","event_id":"aabbccddeeff-f049192a-1","transition":"occupied","uptime_ms":23800,"peak_c":34.2,"pixels":12,"bounds":{"x":20,"y":18,"width":4,"height":3}}
```

`roi` is the stable name of the single configured region; `roi_bounds` additionally gives its configured x/y/width/height. `peak_c` is the highest current temperature anywhere in that ROI. `pixels` and `bounds` describe its largest qualifying connected component, not a count of people/animals. During the clear delay, occupancy remains `occupied` even when component size is zero. `peak_c` is null during `learning`/`unavailable`; an exit event reports the remaining scene temperature, not the departed object's historic peak.

`event_id` combines device ID, a random boot identifier and monotonically incrementing transition sequence. `uptime_ms` is a 32-bit monotonic boot clock (wraps after about 49.7 days), not UTC; on events it identifies the transition time. QoS 1 duplicates must be de-duplicated using `event_id`.

From 0.2.0, the existing `/state` and `/event` remain aggregate occupancy. `/small/state`, `/large/state`, `/small/event` and `/large/event` use the same envelope with `object_class` set to `small` or `large` (`any` for aggregate). States are retained; events are not. Each channel has its own sequence and event IDs include a class suffix to prevent collisions. Treat IDs as opaque. For class payloads, peak temperature follows the class-specific rules above rather than the entire ROI. All channels share `/availability` and independently discard obsolete/offline transitions on reconnect.

Home Assistant discovery publishes three retained QoS-1 binary sensor configs at `homeassistant/binary_sensor/thermal_<id>/<any|small|large>/config`, grouped by `device.identifiers=["thermal_<id>"]`. Each entity requires both device availability and a valid (`clear`/`occupied`) channel state; warm-up, learning and sensor failure appear unavailable. Configs/state are republished on MQTT reconnection and the standard `homeassistant/status` online birth message, without resetting live event cursors. Discovery is staggered to fit the 4096-byte outbox. Turning `ha_discovery` off sends empty retained payloads to the same discovery topics at the next broker connection; keep the broker configured until cleanup occurs. The prefix/birth topic are fixed to Home Assistant defaults in this release. See [official discovery documentation](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery).

The retained state publishes on connect, state changes and at least every 30 seconds while connected. Initial learning completion publishes clear state without a synthetic exit event. Sensor failure/relearning publish state changes without occupied/clear events. A new connection baselines the event sequence, discards the previous client's retransmission outbox, and publishes only current state. Delayed unconsumed transitions older than one second are dropped. A message already accepted by the broker cannot be retracted. MQTT is not a durable event history, and network outages do not stop detection.

To inspect a real device with an existing broker (replace host and device ID):

```text
mosquitto_sub -h broker.local -q 1 -v -t 'thermal/aabbccddeeff/#'
```

Synthetic test-pattern builds deliberately never connect or publish MQTT.

## USB diagnostic console (0.1.4)

The USB composite device provides UVC video and CDC serial using one TinyUSB stack. Board-menu CDC-on-boot stays disabled because the sketch registers CDC explicitly. Open the diagnostic COM port at 115200 with DTR and RTS enabled. Send one newline-terminated command (at most 4095 bytes including its prefix). Commands do not echo credentials. From 0.1.5, baud-rate and control-line changes do not reboot the board; use `BOOTLOADER` or physical BOOT/reset explicitly. `scripts/usb-command.ps1` sets the control lines in order and sends one command per open.

| Command | Result |
| --- | --- |
| `STATUS` | Same health JSON as HTTP |
| `CONFIG` | Saved configuration with passwords redacted |
| `DISCOVERY` | Read-only array of the three generated discovery topics/payloads; empty payloads when discovery is disabled |
| `SET {json}` | Same partial settings validation/persistence as HTTP PUT |
| `VIDEO OFF` / `VIDEO ON` | Temporarily stop/start YUY2 output to USB/browser/snapshots; acquisition/detection continue |
| `SENSOR RECOVER` | Reset capture on the acquisition task, preserving calibration and allocated buffers. Restarts the conservative 120-second detection settling hold. Does not reboot the ESP32 or erase settings. Use `REBOOT` if initial sensor startup failed or the fault persists. |
| `REBOOT` | Restart the application |
| `BOOTLOADER` | Enter ROM download mode for uploading |

0.1.4 status adds `config_loaded` and `config_load_error` (startup load result), `network_task_running`, `wifi_status`, `wifi_disconnect_reason`, `wifi_disconnects`, `ap_active`, `ap_clients`, `ap_ip`, `video_enabled`, `heap_alloc_failures`, `last_failed_alloc_size` and `last_failed_alloc_caps`. Wi-Fi reason/status and memory capability fields use the pinned SDK's numeric enums/flags. Last disconnect reason is historical and can remain nonzero after reconnection. Diagnostic video pause is not persisted. These features are implemented for commissioning; hardware validation is tracked separately.
