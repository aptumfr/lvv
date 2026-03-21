# LVV API Reference

All HTTP endpoints return JSON with `Content-Type: application/json` unless noted otherwise.
On error, endpoints return `{"error": "message"}` with an appropriate HTTP status code.

Base URL: `http://localhost:8080` (configurable with `--web-port`)

---

## Connection

### GET /api/health

Server and target status. Polled by the web UI every 3 seconds.

**Response:**
```json
{
  "status": "ok",
  "connected": true,
  "streaming": false,
  "clients": 2
}
```

### GET /api/ping

Ping the connected LVGL target and get its spy version.

**Response:**
```json
{ "version": "1.0.0" }
```

### POST /api/connect

Connect to the LVGL target (TCP or serial, configured at startup).

**Request body:** `{}` (empty JSON object)

**Response:**
```json
{ "connected": true }
```

### POST /api/disconnect

Disconnect from the target. Stops screenshot streaming if active.

**Response:**
```json
{ "disconnected": true }
```

---

## Widget Tree

### GET /api/tree

Fetch the full widget hierarchy from the target.

**Response:**
```json
{
  "id": 1,
  "name": "screen",
  "type": "obj",
  "x": 0, "y": 0, "width": 800, "height": 480,
  "visible": true,
  "clickable": false,
  "auto_path": "screen",
  "text": "",
  "children": [ ... ]
}
```

### GET /api/widget/\<name\>

Get all properties of a widget by name.

**Example:** `GET /api/widget/btn_ok`

**Response:** Target-dependent JSON object with all widget properties.

### GET /api/screen-info

Get display dimensions and color format.

**Response:**
```json
{
  "width": 800,
  "height": 480,
  "color_format": "ARGB8888"
}
```

---

## Widget Lookup

### POST /api/find-at

Find the deepest widget at screen coordinates. Used for smart recording (click on
canvas → resolve to widget selector).

**Request body:**
```json
{ "x": 150, "y": 200 }
```

**Response:**
```json
{
  "found": true,
  "name": "btn_ok",
  "type": "button",
  "auto_path": "screen/btn_ok",
  "text": "OK",
  "x": 100, "y": 180, "width": 80, "height": 40,
  "selector": "btn_ok"
}
```

The `selector` field returns the best identifier for the widget: `name` if set,
otherwise `auto_path`, otherwise `null`.

### POST /api/find-by

Find widgets using multi-property selectors. Selector syntax is comma-separated
`key=value` pairs. All specified properties must match.

Supported keys: `type`, `name`, `text`, `visible`, `clickable`, `auto_path`.

**Request body (single match):**
```json
{ "selector": "type=button,text=OK" }
```

**Response:**
```json
{
  "found": true,
  "name": "btn_ok",
  "type": "button",
  "auto_path": "screen/btn_ok",
  "text": "OK",
  "x": 100, "y": 180, "width": 80, "height": 40,
  "visible": true,
  "clickable": true
}
```

**Request body (find all):**
```json
{ "selector": "type=button,visible=true", "all": true }
```

**Response:**
```json
{
  "found": true,
  "count": 3,
  "widgets": [
    { "name": "btn_ok", "type": "button", "text": "OK", ... },
    { "name": "btn_cancel", "type": "button", "text": "Cancel", ... },
    { "name": "btn_settings", "type": "button", "text": "Settings", ... }
  ]
}
```

---

## Interaction

### POST /api/click

Click a widget by name or by coordinates.

**By name:**
```json
{ "name": "btn_ok" }
```

**By coordinates:**
```json
{ "x": 150, "y": 200 }
```

**Response:**
```json
{ "success": true }
```

### POST /api/press

Begin a pointer press at coordinates.

**Request body:**
```json
{ "x": 150, "y": 200 }
```

**Response:**
```json
{ "success": true }
```

### POST /api/release

Release the pointer.

**Request body:** none

**Response:**
```json
{ "success": true }
```

### POST /api/move

Move the pointer to coordinates (while pressed, this is a drag).

**Request body:**
```json
{ "x": 200, "y": 200 }
```

**Response:**
```json
{ "success": true }
```

### POST /api/swipe

Swipe gesture between two points.

**Request body:**
```json
{
  "x": 100, "y": 200,
  "x_end": 300, "y_end": 200,
  "duration": 300
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| x, y | int | 0 | Start coordinates |
| x_end, y_end | int | 0 | End coordinates |
| duration | int | 300 | Duration in milliseconds |

**Response:**
```json
{ "success": true }
```

### POST /api/long-press

Press, hold for a duration, then release. Triggers LVGL's `LV_EVENT_LONG_PRESSED`.

**Request body:**
```json
{ "x": 150, "y": 200, "duration": 500 }
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| x, y | int | required | Press coordinates |
| duration | int | 500 | Hold duration in milliseconds |

**Response:**
```json
{ "success": true }
```

### POST /api/drag

Press, move in interpolated steps, then release. Use for sliders, list reordering,
or scroll gestures.

**Request body:**
```json
{
  "x": 100, "y": 200,
  "x_end": 300, "y_end": 200,
  "duration": 300,
  "steps": 10
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| x, y | int | 0 | Start coordinates |
| x_end, y_end | int | 0 | End coordinates |
| duration | int | 300 | Total duration in milliseconds |
| steps | int | 10 | Number of intermediate move events |

**Response:**
```json
{ "success": true }
```

### POST /api/type

Type a text string into the focused widget.

**Request body:**
```json
{ "text": "hello world" }
```

**Response:**
```json
{ "success": true }
```

### POST /api/key

Send a single key event.

**Request body:**
```json
{ "key": "Enter" }
```

**Response:**
```json
{ "success": true }
```

---

## Screen Capture

### GET /api/screenshot

Capture the current screen as a PNG image.

**Response:** Binary PNG data with `Content-Type: image/png`.

---

## Visual Regression

### POST /api/visual/compare

Compare the current screen against a reference PNG image.

On first run (no reference exists), the current screenshot is saved as the new
reference and the test passes automatically.

Reference paths are sandboxed to the server's working directory.

**Request body:**
```json
{
  "reference": "ref_images/home.png",
  "threshold": 0.1,
  "color_threshold": 10.0
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| reference | string | required | Path to reference image |
| threshold | float | 0.1 | Max allowed diff percentage (0-100) |
| color_threshold | float | 10.0 | Per-channel tolerance (0-255) |

**Response (first run):**
```json
{
  "first_run": true,
  "passed": true,
  "message": "Reference image created"
}
```

**Response (comparison):**
```json
{
  "passed": true,
  "identical": false,
  "diff_percentage": 0.02,
  "diff_pixels": 384,
  "total_pixels": 384000
}
```

---

## Test Execution

### POST /api/test/run

Run Python test code or test files.

**Run inline code:**
```json
{ "code": "import lvv\nlvv.click('btn_ok')" }
```

**Response:**
```json
{
  "success": true,
  "output": "OK\n"
}
```

**Run test files:**
```json
{ "files": ["tests/test_navigation.py", "tests/test_settings.py"] }
```

**Response:**
```json
{
  "passed": true,
  "total": 2,
  "pass_count": 2,
  "fail_count": 0,
  "duration": 3.45,
  "tests": [
    {
      "name": "test_navigation.py",
      "status": "pass",
      "duration": 1.2,
      "message": "",
      "output": "Navigation test passed!\n"
    },
    {
      "name": "test_settings.py",
      "status": "pass",
      "duration": 2.25,
      "message": "",
      "output": "Settings tests passed!\n"
    }
  ]
}
```

---

## Widget Lookup (additional)

### GET /api/find

Find a widget by name or auto-path.

**Query parameters:** `name` (required)

**Example:** `GET /api/find?name=btn_ok`

**Response (found):**
```json
{
  "found": true,
  "name": "btn_ok",
  "type": "button",
  "auto_path": "btn_ok",
  "text": "OK",
  "x": 100, "y": 180, "width": 80, "height": 40,
  "visible": true, "clickable": true,
  "selector": "btn_ok"
}
```

**Response (not found):**
```json
{ "found": false }
```

### GET /api/widgets

Get a flat list of all widgets in the tree.

**Response:**
```json
[
  { "name": "screen", "type": "obj", "x": 0, "y": 0, "width": 800, "height": 480, "visible": true, "clickable": true, "auto_path": "obj", "text": "" },
  { "name": "btn_ok", "type": "button", "x": 100, "y": 180, "width": 80, "height": 40, "visible": true, "clickable": true, "auto_path": "btn_ok", "text": "OK" }
]
```

---

## Log Capture

### GET /api/logs

Get captured LVGL log entries.

**Response:**
```json
{ "logs": ["[Warn] lv_refr.c:388 ...", "..."] }
```

### POST /api/logs/clear

Clear the log buffer.

**Response:**
```json
{ "success": true }
```

### POST /api/logs/capture

Enable or disable log capture on the target.

**Request body:**
```json
{ "enable": true }
```

**Response:**
```json
{ "success": true }
```

---

## Performance Metrics

### GET /api/metrics

Get spy performance metrics.

**Response:**
```json
{ "poll_rate": 195, "uptime_ms": 12345 }
```

| Field | Description |
|---|---|
| `poll_rate` | Spy loop iterations per second |
| `uptime_ms` | LVGL uptime in milliseconds |

---

## WebSocket

### WS /ws

Real-time bidirectional channel for screenshot streaming and interaction commands.
Frames are sent as binary JPEG blobs (quality 80%).

**Client → Server messages:**

Start streaming:
```json
{ "type": "start_stream", "fps": 10 }
```

Stop streaming:
```json
{ "type": "stop_stream" }
```

Interaction commands (same fields as REST equivalents):
```json
{ "type": "click_at", "x": 150, "y": 200 }
{ "type": "click", "name": "btn_ok" }
{ "type": "press", "x": 150, "y": 200 }
{ "type": "release" }
{ "type": "move_to", "x": 200, "y": 200 }
{ "type": "swipe", "x": 100, "y": 200, "x_end": 300, "y_end": 200, "duration": 300 }
{ "type": "type", "text": "hello" }
{ "type": "key", "key": "ENTER" }
```

**Server → Client messages:**

Binary: JPEG screenshot frame (during streaming).

**Behavior:**
- Streaming auto-stops when the last client disconnects.
- Streaming stops on target disconnect or screenshot failure.
- Interaction commands are fire-and-forget (no response).
