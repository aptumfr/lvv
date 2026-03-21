# Spy Protocol

The spy (`lvv_spy.c`) communicates over TCP using newline-delimited JSON commands with one exception: screenshot responses use a binary transfer.

## Connection

- TCP server, one client at a time
- Default port: 5555
- Non-blocking I/O (safe to call from LVGL main loop)
- Serial transport uses STX/ETX framing instead of newlines

## Command Format

Request: JSON object with `"cmd"` field, terminated by `\n`.

```
{"cmd":"ping"}\n
```

Response: JSON object terminated by `\n`.

```
{"version":"0.1.0"}\n
```

Errors:

```
{"error":"Widget not found"}\n
```

## Commands

### ping

```json
-> {"cmd": "ping"}
<- {"version": "0.1.0"}
```

### get_tree

Returns the full widget tree rooted at the active screen.

```json
-> {"cmd": "get_tree"}
<- {"tree": {"type": "obj", "name": "", "auto_path": "obj", "text": "", "x": 0, "y": 0, "width": 480, "height": 320, "visible": true, "clickable": false, "id": 12345, "children": [...]}}
```

### find

Find a widget by name or auto-path.

```json
-> {"cmd": "find", "name": "btn_login"}
<- {"widget": {"type": "button", "name": "btn_login", ...}}
```

### click

Click center of a named widget.

```json
-> {"cmd": "click", "name": "btn_login"}
<- {"success": true}
```

### click_at

Click at screen coordinates.

```json
-> {"cmd": "click_at", "x": 100, "y": 200}
<- {"success": true}
```

### press / release / move_to

Low-level touch input.

```json
-> {"cmd": "press", "x": 100, "y": 200}
<- {"success": true}

-> {"cmd": "move_to", "x": 150, "y": 200}
<- {"success": true}

-> {"cmd": "release"}
<- {"success": true}
```

### swipe

Interpolated swipe gesture.

```json
-> {"cmd": "swipe", "x": 100, "y": 200, "x_end": 300, "y_end": 200, "duration": 300}
<- {"success": true}
```

### type

Inject text into focused widget.

```json
-> {"cmd": "type", "text": "hello"}
<- {"success": true}
```

### key

Send a key event. Supported keys: `UP`, `DOWN`, `LEFT`, `RIGHT`, `ENTER`, `ESC`, `BACKSPACE`, `NEXT`, `PREV`.

```json
-> {"cmd": "key", "key": "ENTER"}
<- {"success": true}
```

### screenshot

Binary screenshot transfer. The response is a JSON header line followed by raw pixel bytes.

```
-> {"cmd": "screenshot"}\n
<- {"format": 16, "width": 480, "height": 320, "stride": 1920, "data_size": 614400}\n
<- [614400 raw bytes]
```

Fields:
- `format` - LVGL color format enum (16 = ARGB8888, 17 = XRGB8888, 15 = RGB888, 7 = RGB565)
- `stride` - bytes per row (may include padding)
- `data_size` - total bytes following the header (`stride * height`)

### get_screen_info

```json
-> {"cmd": "get_screen_info"}
<- {"width": 480, "height": 320, "color_format": "ARGB8888"}
```

### get_props

Get widget properties.

```json
-> {"cmd": "get_props", "name": "count_label"}
<- {"type": "label", "x": 100, "y": 50, "width": 80, "height": 30, "visible": true, "clickable": false, "text": "42"}
```

### get_logs

Get captured LVGL log entries.

```json
-> {"cmd": "get_logs"}
<- {"logs": ["[Warn] lv_refr.c:388 ...", "..."]}
```

### clear_logs

Clear the log buffer.

```json
-> {"cmd": "clear_logs"}
<- {"success": true}
```

### set_log_capture

Enable or disable LVGL log capture. When enabled, the spy installs a log callback
that stores entries in a ring buffer (64 max). The app's original log callback is
preserved and forwarded.

```json
-> {"cmd": "set_log_capture", "enable": 1}
<- {"success": true, "capturing": true}
```

### get_metrics

Get spy performance metrics.

```json
-> {"cmd": "get_metrics"}
<- {"poll_rate": 195, "uptime_ms": 12345}
```

## Client-Side Gestures

The following operations are NOT spy protocol commands — they are implemented client-side
by the `lvv` host tool using sequences of `press`, `move_to`, and `release`:

- **`long_press(x, y, duration)`** — press, sleep, release
- **`drag(x1, y1, x2, y2, duration)`** — press, interpolated moves, release

The spy only sees the individual primitive commands.

## Auto-Path System

When widgets don't have explicit names, the spy generates auto-paths:

1. If the widget has a name (via `lv_obj_set_name()`): use the name directly
2. If the widget has text (label text or child label): `type[text]` e.g., `button[Submit]`
3. Otherwise: `type[index]` e.g., `slider[0]` (index among same-type siblings)

Auto-paths are searchable via `find` and `click` commands.

## Input Injection

The spy creates a virtual LVGL input device (`lv_indev_t`). Touch events go through LVGL's normal input pipeline, so scrolling, gestures, long-press, and all other LVGL input behaviors work correctly.

After setting the indev state, the spy calls `lv_timer_ready()` + `lv_timer_handler()` to force immediate processing regardless of the indev timer period.
