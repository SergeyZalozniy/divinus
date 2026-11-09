# API Endpoints Reference

This document describes the REST API endpoints available in Divinus' web server.

## General Response Format

All APIs return responses in JSON format with appropriate HTTP status codes.

## Authentication

If authentication is enabled, all requests must include a basic authentication header:

```
Authorization: Basic [ENCODED_CREDENTIALS]
```

where \[ENCODED_CREDENTIALS\] is a string representing a Base64-encoded string formatted this way: `username:password`. For example, if your username was "admin", and your password "secret", the header's value would be encoded from "admin:secret" to "YWRtaW46c2VjcmV0".

## Endpoints

### System Management

#### `/api/cmd`

Calls various integrated commands.

| Method | Parameters | Description                     |
|--------|------------|---------------------------------|
| GET    | `save`     | Saves the current configuration |
| GET    | `restart`  | Restarts the streaming service  |

**Response**
```json
{
  "code": 0  // 0 = success, !0 = error code
}
```

#### `/api/isp`

Configures ISP (image signal processor) parameters.

| Method | Parameters   | Description                |
|--------|--------------|----------------------------|
| GET    | `mirror`     | Enable/disable mirroring   |
| GET    | `flip`       | Enable/disable flipping    |
| GET    | `antiflicker`| Antiflicker in Hz (-1=off) |

**Response**
```json
{
  "mirror": false,
  "flip": false,
  "antiflicker": 60
}
```

#### `/api/status`

Gets live system status information.

| Method | Parameters | Description                |
|--------|------------|----------------------------|
| GET    | none       | Returns system information |

**Response**
```json
{
  "chip": "GK7205V300",
  "loadavg": [0.05, 0.07, 0.06],
  "memory": "62/121MB",
  "sensor": "imx335",
  "uptime": "7 days, 14:08:57"
}
```

#### `/api/time`

Configures or reads the real-time clock.

| Method | Parameters | Description                                |
|--------|------------|--------------------------------------------|
| GET    | `fmt`      | Time display format (used by dynamic OSDs) |
| GET    | `ts`       | Unix timestamp to set (assuming local TZ)  |

**Response**
```json
{
  "fmt": "%Y-%m-%d %H:%M:%S",
  "ts": 1716633815
}
```

### Video Configuration

#### `/api/jpeg`

Configures JPEG capture parameters.

| Method | Parameters | Description             |
|--------|------------|-------------------------|
| GET    | `width`    | Image width (px)        |
| GET    | `height`   | Image height (px)       |
| GET    | `qfactor`  | Quality factor (1-100%) |

**Response**
```json
{
  "enable": true,
  "width": 1920,
  "height": 1080,
  "qfactor": 80
}
```

#### `/api/mjpeg`

Configures the MJPEG stream.

| Method | Parameters | Description                     |
|--------|------------|---------------------------------|
| GET    | `enable`   | Enable/disable MJPEG stream     |
| GET    | `width`    | Video width (px)                |
| GET    | `height`   | Video height (px)               |
| GET    | `fps`      | Frames per second               |
| GET    | `mode`     | Compression mode (CBR, VBR, QP) |

**Response**
```json
{
  "enable": true,
  "width": 640,
  "height": 480,
  "fps": 15,
  "mode": "CBR",
  "bitrate": 1000000
}
```

#### `/api/mp4`

Configures the H.26x MP4 stream.

| Method | Parameters | Description                                |
|--------|------------|--------------------------------------------|
| GET    | `enable`   | Enable/disable MP4 stream                  |
| GET    | `width`    | Video width (px)                           |
| GET    | `height`   | Video height (px)                          |
| GET    | `fps`      | Frames per second                          |
| GET    | `gop`      | Group of pictures length in frames         |
| GET    | `bitrate`  | Bits per second                            |
| GET    | `h265`     | Use H.265 instead of H.264                 |
| GET    | `mode`     | Compression mode (CBR, VBR, QP, ABR, AVBR) |
| GET    | `profile`  | Profile (BP/BASELINE, MP/MAIN, HP/HIGH)    |
| GET    | `intra_refresh` | Enable/disable intra refresh (GDR)    |
| GET    | `intra_refresh_mode` | Refresh sweep (row, column)      |
| GET    | `intra_refresh_num` | Rows or columns refreshed per frame |
| GET    | `intra_refresh_qp` | QP of requested I-frames (0-51)    |
| GET    | `ip_qp_delta` | I/P QP delta (-10 to 30, auto)          |

**Response**
```json
{
  "enable": true,
  "width": 1280,
  "height": 720,
  "fps": 30,
  "gop": 30,
  "h265": false,
  "mode": "VBR",
  "profile": "MP",
  "bitrate": 2000000,
  "intra_refresh": false,
  "intra_refresh_mode": "row",
  "intra_refresh_num": 1,
  "intra_refresh_qp": 0,
  "ip_qp_delta": "auto"
}
```

#### `/api/sub`

Configures the H.26x sub stream (RTSP only), same parameters and response as `/api/mp4`. Enabling it at runtime works, but its video buffer pool is only sized at startup, so save and restart after changing its size.

### Audio Configuration

#### `/api/audio`

Configures the audio parameters.

| Method | Parameters | Description                   |
|--------|------------|-------------------------------|
| GET    | `enable`   | Enable/disable audio          |
| GET    | `bitrate`  | Bits per second               |
| GET    | `gain`     | Audio gain (dB amplification) |
| GET    | `srate`    | Sample rate in Hz             |

**Response**
```json
{
  "enable": true,
  "bitrate": 32000,
  "gain": 50,
  "srate": 16000
}
```

### Night Mode

#### `/api/night`

Configures the night mode parameters.

| Method | Parameters     | Description               |
|--------|----------------|---------------------------|
| GET    | `enable`       | Enable/disable night mode |
| GET    | `manual`       | Manual or automatic mode  |
| GET    | `grayscale`    | Switch to grayscale       |
| GET    | `ircut`        | Control IR-Cut filter     |
| GET    | `ircut_pin1`   | GPIO pin 1 for IR-Cut     |
| GET    | `ircut_pin2`   | GPIO pin 2 for IR-Cut     |
| GET    | `irled`        | Control IR LEDs (reads `false` without an `irled_pin`) |
| GET    | `irled_follow` | IR LEDs follow day/night (`false` = always off) |
| GET    | `irled_pin`    | GPIO pin for IR LEDs      |
| GET    | `irsense_pin`  | GPIO pin for IR sensor    |
| GET    | `irsense_invert` | Low sensor level = night |
| GET    | `night_delay_s`| Darkness hold before night|
| GET    | `day_delay_s`  | Daylight hold before day  |
| GET    | `day_min_lum`  | Min. picture luma for day |
| GET    | `day_max_gain` | Max. analog gain for day  |
| GET    | `adc_device`   | ADC device                |
| GET    | `adc_threshold`| Activation threshold      |

**Response**
```json
{
  "active": true,
  "manual": false,
  "grayscale": true,
  "ircut": true,
  "ircut_pin1": 17,
  "ircut_pin2": 18,
  "irled": true,
  "irled_follow": true,
  "irled_pin": 20,
  "irsense_pin": 21,
  "irsense_invert": false,
  "night_delay_s": 20,
  "day_delay_s": 60,
  "day_min_lum": 20,
  "day_max_gain": 0,
  "isp_lum": 54,
  "isp_again": 3313,
  "adc_device": "/dev/adc",
  "adc_threshold": 800
}
```

#### Majestic compatibility, for tools such as BlueOS Video Streams

With `system.web_majestic_compat: true` (off by default) the server also answers the day/night subset of the HTTP API of majestic, OpenIPC's stock streamer, so tools written for it detect the camera and switch day/night unchanged. These routes sit behind the same authentication as every other endpoint, all changes are RAM only (like `/api/night`), and `/api/night` keeps working alongside them.

| Request | Response (200) | Effect |
|---------|----------------|--------|
| `GET /api/v1/config.json` | JSON, see below | None |
| `GET /metrics/night` | Prometheus text, see below | None |
| `GET /night` | `1` (night) or `0` (day) | None |
| `GET /night/on` | `1` | Manual mode, then night: grayscale on, IR-cut filter out, IR LEDs on (if `backlight` is `auto`) |
| `GET /night/off` | `0` | Manual mode, then day: grayscale off, IR-cut filter in, IR LEDs off |
| `GET /night/toggle` | `1` or `0`, the new state | Manual mode, then the opposite of the current state |
| `GET /api/v1/set?nightMode.lightMonitor=true` | `true` (or `false` when night mode is disabled in the config) | Automatic mode: the night thread restarts and the light sensor decides, like `/api/night?manual=false` |
| `GET /api/v1/set?nightMode.lightMonitor=false` | `false` | Manual mode, keeping the current picture |
| `GET /api/v1/set?nightMode.backlight=auto` | `auto` | IR LEDs follow day/night (on at night, off by day), like `/api/night?irled_follow=true`; turns them on at once if it is night |
| `GET /api/v1/set?nightMode.backlight=off` | `off` | IR LEDs stay off, also at night; turns them off at once |
| `GET /api/v1/set?nightMode.backlight=<other>` | 400 `nightMode.backlight must be auto or off` | None (majestic's `manual` is not supported); keys before it in the same query are applied |
| `GET /api/v1/set?<any other key>=...` | `OK` | Ignored |

Bodies are `text/plain` without a trailing newline, except `config.json`. `/night/on` and `/night/off` only pulse the IR-cut coil when the state actually changes. `lightMonitor` is reported as `true` only while night mode is enabled and not in manual mode. `backlight` mirrors `night_mode.ir_led_follow` (`auto` = follow, `off` = never); it is accepted even without an `ir_led_pin` (nothing to drive then). `light` is the current IR LED output, `false` without an `ir_led_pin`.

`/api/v1/config.json` (pins at `999`, i.e. unused, read `null`):
```json
{
  "nightMode": {
    "enabled": true,
    "lightMonitor": true,
    "colorToGray": true,
    "irCutPin1": 11,
    "irCutPin2": 10,
    "backlightPin": null,
    "backlight": "auto",
    "light": false,
    "lightSensorPin": 65,
    "lightSensorInvert": true,
    "autoNightDelay": 20,
    "autoDayDelay": 180,
    "pinSwitchDelayUs": 150,
    "night": false,
    "manual": false
  },
  "system": { "webPort": 80, "streamer": "divinus" }
}
```

`/metrics/night` (`# HELP`/`# TYPE` lines omitted here):
```
night_enabled 0
night_mode_source 1
ircut_enabled 1
light_enabled 0
grayscale_enabled 0
```
`night_enabled` is the current state (night when grayscale and filter out, whatever the IR LEDs do), `light_enabled` is the IR LED output (`0` without an `ir_led_pin`), `night_mode_source` is `0` in manual mode and `1` while the light sensor decides.

### On-Screen Display (OSD)

#### `/api/osd/{id}`

Configures text or image overlays by their ID (0-9 at the moment).

| Method | Parameters | Description                                        |
|--------|------------|----------------------------------------------------|
| GET    | `text`     | Text to display (special specifiers supported)     |
| GET    | `img`      | Local image path to display (`text` must be empty) |
| GET    | `font`     | Font name to be used                               |
| GET    | `size`     | Font size (decimal in pt)                          |
| GET    | `color`    | Font color (hex format, RGB555 format)             |
| GET    | `opal`     | Opacity level (0-255)                              |
| GET    | `pos`      | Position on main stream \[x,y\]                    |
| GET    | `posx`     | X coordinate (write-only)                          |
| GET    | `posy`     | Y coordinate (write-only)                          |
| GET    | `outl`     | Outline color (hex format, RGB555 format)          |
| GET    | `thick`    | Outline thickness (0 to disable)                   |
| POST   | `file`     | Bitmap or PNG image to upload (replaces text)      |

**Response**
```json
{
  "id": 0,
  "color": "#ffff",
  "opal": 255,
  "pos": [16, 64],
  "font": "UbuntuMono-Regular",
  "size": 15.0,
  "text": "Backyard (%T)",
  "outl": "#8000",
  "thick": 0.0
}
```

### Recordings

#### `/api/record`

Manages video recording operations.

| Method | Parameters         | Description                                        |
|--------|--------------------|----------------------------------------------------|
| GET    | `continuous`       | Adjusts the operation mode to be uninterruptible   |
| GET    | `path`             | Specifies the location of the resulting files      |
| GET    | `filename`         | Sets the base output name (extension needed)       |
| GET    | `segment_duration` | Sets the maximum segment duration (seconds)        |
| GET    | `segment_size`     | Sets the maximum segment size (bytes)              |
| GET    | `start`            | Starts a new recording session                     |
| GET    | `stop`             | Stops the current recording session                |

**Response**
```json
{
  "recording": true,
  "start_time": "2025-05-08T14:30:00Z",
  "continuous": true,
  "path": "/mnt/sdcard/recordings",
  "filename": "Entrance.mp4",
  "segment_duration": 0,
  "segment_size": 10485760
}
```


## Content Streaming

### `/image.jpg`

Captures an instant JPEG image.

| Method | Parameters    | Description          |
|--------|---------------|----------------------|
| GET    | `width`       | Snapshot width (px)  |
| GET    | `height`      | Snapshot height (px) |
| GET    | `qfactor`     | Quality factor       |
| GET    | `color2gray`  | Convert to grayscale |

**Response**: JPEG file

### `/mjpeg`

Continuous MJPEG stream.

**Response**: multipart/x-mixed-replace stream

### `/video.mp4`

Continuous MP4 video stream.

**Response**: Segmented MP4 video stream

### `/video.264` or `/video.265`

Raw H.264/H.265 stream.

**Response**: Binary data stream

### `/audio.mp3`

Continuous MP3 audio stream.

**Response**: MP3 audio stream

### `/audio.pcm`

Raw PCM audio stream.

**Response**: Uncompressed PCM audio stream

## Special Commands

### `/exit`

Gracefully stops the server.

| Method | Parameters | Description      |
|--------|------------|------------------|
| GET    | none       | Stops the server |

**Response**: Message "Closing..."

### `/`, `/index.htm` or `/index.html`

WebUI homepage.

**Response**: HTML SPA with embedded resources