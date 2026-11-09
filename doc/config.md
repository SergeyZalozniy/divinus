# Configuration Reference

This document describes the fields that can be found within a configuration file for Divinus.

## System section

- **sensor_config**: Path to the sensor calibration or configuration file, if applicable (e.g., `/etc/sensors/imx415.bin`).
- **web_port**: Port number for the web server (default: `80`).
- **web_whitelist**: Array of up to 4 IP addresses or domains allowed to access the web server.
- **web_enable_auth**: Boolean to enable authentication on the API, live stream and WebUI endpoints (default: `false`).
- **web_auth_user**: Username for basic authentication (default: `admin`).
- **web_auth_pass**: Password for basic authentication (default: `12345`).
- **web_auth_skiplocal**: Boolean to skip authentication for local addresses (default: `true`).
- **web_enable_static**: Boolean to enable serving static web content (default: `false`).
- **web_majestic_compat**: Boolean to also serve the day/night subset of majestic's HTTP API (`/api/v1/config.json`, `/metrics/night`, `/night/on|off|toggle`, `/api/v1/set?nightMode.lightMonitor=` and `nightMode.backlight=`), for tools such as BlueOS Video Streams that only know OpenIPC's stock streamer; see the endpoints reference (default: `false`).
- **isp_thread_stack_size**: Stack size for ISP thread, if applicable (default: `16384`).
- **venc_stream_thread_stack_size**: Stack size for video encoding stream thread (default: `16384`).
- **web_server_thread_stack_size**: Stack size for web server thread (default: `65536`).
- **time_format**: Format for displaying time, refer to strftime() modifiers for exact parameters (e.g., `"%Y-%m-%d %H:%M:%S"`).
- **watchdog**: Watchdog timer in seconds, where 0 means disabled (default: `0`).

## Night mode section

- **enable**: Boolean to activate night mode support.
- **ir_cut_pin1**: GPIO number for IR cut filter control (normal state pin).
- **ir_cut_pin2**: GPIO number for IR cut filter control (inverted state pin).
- **ir_led_pin**: GPIO number for IR LED control.
- **ir_led_follow**: Boolean, when `true` the IR LEDs on `ir_led_pin` follow day/night (on at night, off by day); when `false` night mode keeps them off, also at night (default: `true`). Night is judged by grayscale and the IR-cut filter only, so dark LEDs do not count as day. Also `/api/night?irled_follow=` and, in majestic compatibility, `nightMode.backlight=auto|off`.
- **ir_sensor_pin**: GPIO number for PIR motion sensor or similar digital toggle; `999` means no sensor (default).
- **ir_sensor_invert**: Boolean to invert the sensor input, when `true` a low level on `ir_sensor_pin` (or an ADC reading below the threshold) means night (default: `false`).
- **check_interval_s**: Interval in seconds to check night mode conditions.
- **night_delay_s**: Seconds the sensor must keep reporting darkness before switching to night mode, counted in steps of `check_interval_s`; `0` switches on the first differing reading (default: `0`).
- **day_delay_s**: Seconds the sensor must keep reporting daylight before switching back to day mode, same rules as `night_delay_s` (default: `0`).
- **day_min_lum**: In night mode, a daylight reading from the sensor only counts if the average picture luma reported by the ISP auto exposure (0-255) is at least this value; `0` disables the check (default: `0`). Meant for illuminators that switch their IR LEDs off on their own for a while: the sensor then reads daylight, but the picture is nearly black at maximum gain. Only on HiSilicon/Goke v4 for now; elsewhere, or when the ISP cannot report it, the sensor alone decides.
- **day_max_gain**: Same idea with the sensor analog gain: a daylight reading only counts while the gain is at most this value, in 1/1024 steps (`1024` = 1x); `0` disables the check (default: `0`). `/api/night` shows the current `isp_lum` and `isp_again` for tuning both values.
- **pin_switch_delay_us**: Delay in microseconds before switching GPIO pins, must be used to protect cut filter coils from burning.
- **adc_device**: Path to the ADC device used for night mode.
- **lamp**: Illuminator mode on Fullhan V100, combinable: `ir` (default), `white` (colour night vision, IR-cut filter in) or `none`. Combine with commas, e.g. `ir,white`.
- **adc_threshold**: Threshold raw value to trigger night mode, depends on the bitness of the given ADC device.

## ISP section

- **mirror**: Boolean to turn on image mirroring (default: `false`).
- **flip**: Boolean to turn on image flipping (default: `false`).
- **antiflicker**: Antiflicker setting in Hz (default: `60`).

## mDNS section

- **enable**: Boolean to turn on mDNS announcer, lets LAN users access the device by a domain name, its configured hostname followed by .local (default: `false`).

## ONVIF section

- **enable**: Boolean to activate ONVIF services (default: `false`).
- **enable_auth**: Boolean to turn on ONVIF authentication (default: `false`).
- **auth_user**: Username for ONVIF authentication (default: `admin`).
- **auth_pass**: Password for ONVIF authentication (default: `12345`).

## RTSP section

- **enable**: Boolean to activate the integrated RTSP server (default: `true`).
- **enable_auth**: Boolean to turn on RTSP authentication (default: `false`).
- **auth_user**: Username for RTSP authentication (default: `admin`).
- **auth_pass**: Password for RTSP authentication (default: `12345`).
- **port**: Port number for RTSP server (default: `554`).

With the sub stream enabled, the URL path picks the video stream: `/sub` or a path containing `stream=1` serves the sub stream, anything else (`/`, `/main`, `stream=0`, ...) the main one. Without it the path is ignored.

## Record section

- **enable**: Boolean to allow or block recording operations (default: `false`).
- **continuous**: Boolean to turn on continuous recording at launch (default: `false`).
- **path**: Path to save recordings (e.g., `/mnt/sdcard/recordings`). Missing directories are created automatically.
- **filename**: Base name for the resulting files (e.g., `Entrance.mp4`). A `_<timestamp>` suffix is appended before the extension so segments never overwrite each other; leave empty to use `recording`.
- **segment_duration**: Target duration for a recording in seconds
- **segment_size**: Target file size for a recording in bytes

## Stream section

- **enable**: Boolean to turn on special streaming methods (default: `false`).
- **udp_srcport**: Source port for UDP streaming (default: `5600`).
- **dest**: List of destination URLs for streaming (e.g., `- udp://239.255.255.0:5600 or - rtmp://myserver/path/mykey`).

## Audio section

- **enable**: Boolean to activate or deactivate audio functionality.
- **bitrate**: Audio bitrate in kbps (e.g., `128`).
- **gain**: Audio gain in decibels (e.g., `0` for no gain).
- **srate**: Audio sampling rate in Hz (e.g., `44100`).

## MP4 section

- **enable**: Boolean to activate MP4 output (required by live streams and RTSP).
- **codec**: Codec used for encoding (H.264 or H.265).
- **mode**: Encoding mode.
- **width**: Video width in pixels.
- **height**: Video height in pixels.
- **fps**: Frames per second.
- **gop**: Interval between keyframes, in frames (up to 65535).
- **profile**: Encoding profile.
- **bitrate**: Bitrate in kbps.
- **intra_refresh**: Boolean to turn on gradual intra refresh (GDR) for H.264/H.265, spreading intra coding over consecutive frames instead of large periodic keyframes; only on HiSilicon/Goke v4 (Hi3516EV200/300, GK7205V200/300) at the moment (default: `false`).
- **intra_refresh_mode**: Direction of the refresh sweep, `row` or `column` (default: `row`).
- **intra_refresh_num**: Number of rows or columns intra coded per frame (default: `1`).
- **intra_refresh_qp**: QP for the requested I-frames, 0-51 (default: `0`).
- **ip_qp_delta**: QP difference between I and P frames, -10 to 30, or `auto`: GOP length divided by the framerate, or `0` with `intra_refresh`, where the GOP start is not a keyframe and a lower QP would only inflate it; only on HiSilicon/Goke v4 at the moment (default: `auto`).

## Sub section

Optional second H.264/H.265 stream from the same sensor, scaled down by its own VPSS channel and served over RTSP only (see the RTSP section for the URLs). Only on HiSilicon/Goke v4 (Hi3516EV200/300, GK7205V200/300) at the moment; with `enable: false` nothing changes.

- **enable**: Boolean to activate the sub stream (default: `false`).
- **codec**, **mode**, **width**, **height**, **fps**, **gop**, **profile**, **bitrate**: Same meaning as in the MP4 section (defaults: H.264, CBR, 640x360, 15 fps, GOP = fps, MP, 512 kbps).
- **intra_refresh**, **intra_refresh_mode**, **intra_refresh_num**, **intra_refresh_qp**, **ip_qp_delta**: Same meaning and defaults as in the MP4 section.

## OSD section

- **enable**: Boolean to turn on On-Screen Display regions globally, used to reduce resource usage or let another app manage the functionality (default: `true`).
- **regX_img**: Path to the image for OSD region X.
- **regX_text**: Text displayed in OSD region X.
- **regX_font**: Font used for text in OSD region X.
- **regX_opal**: Opacity of OSD region X.
- **regX_posx**: X position of OSD region X.
- **regX_posy**: Y position of OSD region X.
- **regX_size**: Size of the text or image in OSD region X.
- **regX_color**: Color of the text or image in OSD region X.
- **regX_outl**: Outline color of the text in OSD region X.
- **regX_thick**: Thickness of the text outline in OSD region X.

## JPEG section

- **enable**: Boolean to activate JPEG output.
- **width**: Image width in pixels.
- **height**: Image height in pixels.
- **qfactor**: JPEG compression quality factor.

## MJPEG section

- **enable**: Boolean to activate MJPEG output.
- **mode**: Encoding mode.
- **width**: Video width in pixels.
- **height**: Video height in pixels.
- **fps**: Frames per second.
- **bitrate**: Bitrate in kbps.
- **qfactor**: JPEG compression quality factor.

## HTTP POST section

- **enable**: Boolean to activate HTTP POST snapshots.
- **host**: Hostname for HTTP POST requests.
- **url**: URL for HTTP POST requests.
- **login**: Login for HTTP POST authentication.
- **password**: Password for HTTP POST authentication.
- **width**: Image width sent in pixels.
- **height**: Image height sent in pixels.
- **interval**: Interval between requests in seconds.
- **qfactor**: JPEG compression quality factor.
