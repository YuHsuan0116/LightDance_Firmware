# LightDance ESP32 HTTP API

HTTP API exposed by each LightDance ESP32 device through ESP-IDF's `esp_http_server`.

This document describes the **current firmware implementation** in `http_handler.c`, including endpoints that are not yet implemented.

## Base URL

```text
http://<device-ip>[:<port>]
```

The port is determined by the firmware's HTTP server configuration. The examples below assume port `80` and device IP `192.168.1.101`.

Endpoints with JSON request bodies use `Content-Type: application/json`. Responses are JSON.

## Endpoints

| Method | Endpoint | Description | Implementation |
| --- | --- | --- | --- |
| `GET` | `/api/status` | Read the device's current Wi-Fi TSF time | Implemented |
| `POST` | `/api/play` | Schedule a playback command at a TSF timestamp | Implemented; scheduler result is not checked |
| `POST` | `/api/pause` | Pause playback | TODO (no action yet) |
| `POST` | `/api/stop` | Send a stop command | Implemented |
| `POST` | `/api/test` | Run a solid-color or rainbow LED test | Implemented |
| `POST` | `/api/reset` | Reset the device/application | TODO (no action yet) |
| `POST` | `/api/upload` | Request an upload using a URL | TODO (URL is parsed but no upload occurs) |

## Response conventions

Most POST endpoints return HTTP `200 OK` with:

```json
{"ok": true}
```

For an invalid JSON request body or a missing/wrong-type required field, the endpoints that parse JSON return HTTP `400 Bad Request` with:

```json
{"ok": false}
```

**Important:** `{"ok": true}` currently means the HTTP handler reached its success response. It does **not** guarantee that a scheduled operation executed or that a TODO endpoint performed its intended action.

`GET /api/status` returns a different response schema, documented below.

## API reference

### `GET /api/status`

Return the current Wi-Fi Timing Synchronization Function (TSF) timestamp from the device's station interface (`WIFI_IF_STA`).

**Request**

```bash
curl http://192.168.1.101/api/status
```

**Response — `200 OK`**

```json
{
  "tsf_us": 123456789
}
```

| Field | Type | Description |
| --- | --- | --- |
| `tsf_us` | integer (`int64`) | Current device Wi-Fi TSF timestamp, in microseconds |

The value is **not** a Unix timestamp. Its validity depends on the station's Wi-Fi/TSF state.

### `POST /api/play`

Schedule a playback command using an absolute target TSF timestamp.

**Request body**

```json
{
  "play_at": 123556789
}
```

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `play_at` | JSON number | Yes | Target TSF timestamp in microseconds, in the same time base as the device's `tsf_us` |

**Example**

```bash
curl -X POST http://192.168.1.101/api/play \
  -H "Content-Type: application/json" \
  -d '{"play_at":123556789}'
```

**Responses**

- `200 OK` — `{"ok":true}`
- `400 Bad Request` — `{"ok":false}` if JSON is invalid or `play_at` is not a JSON number

**Behavior and limitations**

- The firmware calculates `delay_us = play_at - current_tsf` and passes it to `delayed_execute()`.
- The target must be **in the future**. The current implementation does not reject past timestamps, so a negative delay may be converted to an unexpectedly large unsigned value.
- The handler currently **ignores the return value** of `delayed_execute()`; `200 OK` does not guarantee successful scheduling.
- Although documented as a timestamp, the handler only checks that `play_at` is a JSON number; it does not check integer precision or range.

For client-side scheduling, retrieve `/api/status` first and calculate a future timestamp in that device's TSF time base. Do not assume timestamps taken from separate devices are interchangeable without verifying their synchronization.

### `POST /api/pause`

Reserved for pausing playback.

**Request**

```bash
curl -X POST http://192.168.1.101/api/pause
```

**Response — `200 OK`**

```json
{"ok": true}
```

**Status:** Not implemented. The handler currently logs `pause` and returns success without calling an application pause function.

### `POST /api/stop`

Send a stop command to the application via `app_send_stop()`.

**Request**

```bash
curl -X POST http://192.168.1.101/api/stop
```

**Response — `200 OK`**

```json
{"ok": true}
```

### `POST /api/test`

Trigger an LED test pattern. Supported modes are `solid` and `rainbow`.

#### Solid-color test

**Request body**

```json
{
  "mode": "solid",
  "color": {
    "r": 255,
    "g": 0,
    "b": 0
  }
}
```

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `mode` | string | Yes | Must be `"solid"` for this operation |
| `color` | object | Yes | RGB color object |
| `color.r` | JSON number | Yes | Red component |
| `color.g` | JSON number | Yes | Green component |
| `color.b` | JSON number | Yes | Blue component |

RGB values are normally integers in the range `0`–`255`, but the **current handler does not enforce this range or integer-only values**; it passes `valueint` to `app_send_test_solid()`.

**Example**

```bash
curl -X POST http://192.168.1.101/api/test \
  -H "Content-Type: application/json" \
  -d '{"mode":"solid","color":{"r":255,"g":0,"b":0}}'
```

#### Rainbow test

**Request body**

```json
{
  "mode": "rainbow"
}
```

**Example**

```bash
curl -X POST http://192.168.1.101/api/test \
  -H "Content-Type: application/json" \
  -d '{"mode":"rainbow"}'
```

**Responses**

- `200 OK` — `{"ok":true}`
- `400 Bad Request` — `{"ok":false}` if the JSON is invalid, `mode` is not a string, or a required RGB component for `solid` is not numeric

**Known limitation:** An unknown string `mode` currently returns `200 OK` without running a test, because the handler does not reject unsupported mode names.

### `POST /api/reset`

Reserved for device/application reset.

**Request**

```bash
curl -X POST http://192.168.1.101/api/reset
```

**Response — `200 OK`**

```json
{"ok": true}
```

**Status:** Not implemented. The handler currently only logs `reset` and returns success.

### `POST /api/upload`

Reserved for an upload operation using a supplied URL.

**Request body**

```json
{
  "url": "http://192.168.1.10:8000/firmware.bin"
}
```

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `url` | string | Yes | URL accepted by the handler for a future upload operation |

**Example**

```bash
curl -X POST http://192.168.1.101/api/upload \
  -H "Content-Type: application/json" \
  -d '{"url":"http://192.168.1.10:8000/firmware.bin"}'
```

**Responses**

- `200 OK` — `{"ok":true}` if a valid JSON string `url` is provided
- `400 Bad Request` — `{"ok":false}` if the body is invalid JSON or `url` is not a string

**Status:** Not implemented. The handler currently logs the URL; it does not download or install a file. The URL above is illustrative.

## Current implementation notes

- JSON requests are buffered in a `char buf[256]`. A JSON request body must be **1–255 bytes**; empty or larger bodies are rejected with `400 Bad Request` for the endpoints that call `recv_json()`.
- No authentication or authorization is implemented in the shown HTTP handlers; restrict device network access as appropriate.
- The shown handlers do not explicitly validate the request `Content-Type` header.
- `POST /api/pause`, `/api/reset`, and `/api/upload` acknowledge requests but do not perform their named operations yet.
- `POST /api/play` does not verify scheduler success. Error handling and request validation may change in later versions.
