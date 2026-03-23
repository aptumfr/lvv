# LVV Architecture

LVV is a test automation tool for LVGL-based UIs. It connects to a target running the LVV spy, inspects the widget tree, captures screenshots, injects input, and runs Python test scripts.

## System Overview

```
+-------------------+        TCP/Serial       +-------------------+
|       LVV         | <---------------------> |   Target App      |
|  (host tool)      |   JSON commands +       |  + lvv_spy.c      |
|                   |   binary screenshots    |  (embedded lib)   |
+-------------------+                         +-------------------+
       |
       |  WebSocket + REST
       v
+-------------------+
|   Web Browser     |
|   (React UI)      |
+-------------------+
```

## Layered Architecture

The codebase is still broadly layered, but not as a strict one-direction dependency graph.
The main flow is layered, with a few pragmatic cross-layer dependencies in orchestration and scripting support:

```
  app/          CLI commands, orchestration
    |
  server/       Crow web server, REST API, WebSocket streaming
    |
  scripting/    PocketPy engine, lvv Python module
    |
  core/         Widget tree, test runner, screen capture, visual regression
    |
  protocol/     Command serialization, screenshot transfer
    |
  transport/    TCP socket, serial port (POSIX)
```

### Transport Layer (`src/transport/`)

Abstract `ITransport` interface with two implementations:

- **TCPTransport** - connects to spy over TCP (default, localhost:5555)
- **SerialTransport** - connects over serial/UART with STX/ETX framing

Both support:
- `send()` - newline-delimited JSON
- `receive()` - read one JSON line with timeout
- `receive_bytes()` - read N raw bytes (used for binary screenshot transfer)

### Protocol Layer (`src/protocol/`)

`Protocol` wraps transport with typed commands matching the spy's command set:
- Widget operations: `find`, `click`, `click_at`, `press`, `release`, `move_to`, `swipe`
- Text input: `type_text`, `key`
- Inspection: `get_tree`, `get_props`, `get_screen_info`
- Screenshot: binary transfer (JSON header + raw pixels, no base64)

All commands are synchronous request/response, protected by a mutex.

### Core Layer (`src/core/`)

- **WidgetTree** - parses the spy's JSON tree, provides `find_by_name()` and `find_at(x,y)` coordinate lookup
- **TestRunner** - runs Python test files via ScriptEngine, collects results
- **ScreenCapture** - decodes raw LVGL pixels (ARGB8888, RGB565, etc.) to RGBA, encodes PNG
- **VisualRegression** - pixel-by-pixel image comparison with anti-aliasing detection and configurable thresholds
- **JUnit XML** - generates JUnit-compatible test reports for CI

`TestRunner` is the main exception to a strict layering rule: it lives in `core/` but depends on `ScriptEngine` from `scripting/`.

### Scripting Layer (`src/scripting/`)

- **ScriptEngine** - runs PocketPy on a dedicated thread (PocketPy's VM is thread-local). Supports execution timeout with cancellation.
- **lvv_module** - exposes the `lvv` Python module with bindings for all protocol commands plus `wait`, `wait_for`, `wait_until`, `assert_visible`, `assert_value`, `screenshot_compare`, etc.

### Server Layer (`src/server/`)

- **WebServer** - Crow HTTP server serving the React SPA and REST API
- **ApiRoutes** - REST endpoints for all protocol operations, test execution, visual regression
- **WSHandler** - WebSocket handler for live streaming (binary JPEG frames) and fire-and-forget input commands (press, move, release)

### App Layer (`src/app/`)

Orchestrates everything based on CLI subcommand:
- `serve` - starts web server for interactive use
- `run` - headless test execution with JUnit output
- `ping`, `tree`, `screenshot` - one-shot CLI utilities

In practice, think of the architecture as:
- layered data path: transport -> protocol -> higher-level services
- orchestration layer at the top: app/server wiring the subsystems together
- a few intentional convenience dependencies where strict purity would add more complexity than value

## Spy (`spy/lvv_spy.c`)

Single-file C library embedded in the target application. Features:
- TCP server accepting one client at a time
- Non-blocking I/O via poll()
- Newline-delimited JSON command protocol
- Widget tree serialization with auto-path system (e.g., `button[Submit]`, `slider[0]`)
- Input injection via a virtual LVGL indev (press/move/release through LVGL's normal input pipeline)
- Binary screenshot transfer (JSON header + raw pixel bytes)
- Named widget support via `LV_USE_OBJ_NAME`

## Web Frontend (`web/`)

React + TypeScript SPA with Tailwind CSS and Zustand state management.

### Components

| Component | Purpose |
|-----------|---------|
| LiveView | Canvas-based live streaming + mouse interaction |
| WidgetTree | Interactive tree browser with expand/collapse |
| PropertyPanel | Shows selected widget properties |
| ConnectionPanel | Connect/disconnect controls |
| TestEditor | Python test editor with recording mode |
| VisualDiffViewer | Visual regression comparison tool |

### Communication

- **REST API** for request/response operations (tree fetch, test execution, visual compare)
- **WebSocket** for real-time streaming (binary JPEG frames at configurable FPS) and input forwarding (press/move/release as fire-and-forget messages)

## Network Services & Ports

LVV involves two network-facing processes. This section is intended for security review.

### Spy TCP Server (target side)

| Property | Value |
|----------|-------|
| Process | Target application (linked with `lvv_spy.c`) |
| Default port | **5555** (configurable via `lvv_spy_init(port)`) |
| Protocol | TCP, newline-delimited JSON + binary screenshot data |
| Bind address | `0.0.0.0` (all interfaces) |
| Max clients | 1 (single-client, rejects additional connections) |
| Authentication | None |
| Encryption | None (plaintext) |

The spy accepts any TCP connection and processes commands that can:
- Read the full widget tree (UI structure, text content, coordinates)
- Inject touch input (press, move, release, click, swipe)
- Inject keyboard input and text
- Capture screenshots (raw framebuffer pixels)

**Risk**: An attacker with network access to the spy port can observe and fully control the UI. The spy should only be enabled in development/test builds or on isolated networks.

### LVV Web Server (host side)

| Property | Value |
|----------|-------|
| Process | `lvv serve` |
| Default port | **8080** (`--web-port`) |
| Bind address | `0.0.0.0` (all interfaces) |
| Protocol | HTTP/1.1 (Crow) |
| Authentication | None |
| Encryption | None (plaintext HTTP) |

#### HTTP REST Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/health` | Server status, connection state |
| POST | `/api/connect` | Connect to target spy |
| POST | `/api/disconnect` | Disconnect from target |
| GET | `/api/ping` | Ping target, get spy version |
| GET | `/api/tree` | Full widget tree JSON |
| GET | `/api/screenshot` | Current screenshot as PNG |
| GET | `/api/screen-info` | Display dimensions and color format |
| GET | `/api/widget/<name>` | Widget properties by name |
| POST | `/api/click` | Click widget by name or coordinates |
| POST | `/api/press` | Touch press at coordinates |
| POST | `/api/move` | Touch move to coordinates |
| POST | `/api/release` | Touch release |
| POST | `/api/swipe` | Swipe gesture |
| POST | `/api/type` | Type text into focused widget |
| POST | `/api/key` | Send key event |
| POST | `/api/find-at` | Find widget at coordinates |
| POST | `/api/test/run` | Execute test code or test files |
| POST | `/api/visual/compare` | Visual regression comparison |
| GET | `/*` | Static files (React SPA from `web/dist/`) |

#### WebSocket Endpoint

| Property | Value |
|----------|-------|
| Path | `/ws` |
| Direction | Bidirectional |
| Server → Client | Binary JPEG frames (live screenshot streaming) |
| Client → Server | JSON messages: `press`, `move`, `release` (input forwarding) |

#### Outbound Connection (host → target)

| Property | Value |
|----------|-------|
| Destination | Target spy (`--host` / `--port`) |
| Default | `localhost:5555` |
| Protocol | TCP, same as spy protocol above |

### Serial Transport (alternative to TCP)

| Property | Value |
|----------|-------|
| Device | `--serial` (e.g., `/dev/ttyUSB0`) |
| Default baud | 115200 (`--baud`) |
| Framing | STX/ETX byte framing |
| Protocol | Same JSON commands as TCP |

No network ports are opened when using serial transport (except the web server if `serve` is used).

### Security Considerations

- **No authentication or encryption** on any channel. LVV is a development/test tool, not intended for production or untrusted networks.
- **Spy exposes full UI control**: any client connected to the spy port can read all visible text, inject arbitrary input, and capture screenshots.
- **`/api/test/run` executes arbitrary code**: the `code` field runs Python scripts via PocketPy. The web server should not be exposed to untrusted users.
- **`/api/visual/compare` file access**: reference image paths are sandboxed to the working directory via `weakly_canonical()` to prevent path traversal.
- **Recommended deployment**: bind to `localhost` or use on isolated test networks only. Use firewall rules to restrict access to ports 5555 and 8080 in shared environments.

## Data Flow: Screenshot Streaming

```
spy: strip-based rendering via LVGL private draw API -> raw pixels (strips of 60 rows)
  |
  | TCP: JSON header {"width":480,"height":320,"stride":1920,"data_size":614400}
  | TCP: raw pixel bytes (streamed per strip)
  v
lvv: decode_raw_pixels() -> RGBA Image
  |
  | encode_jpeg() (libjpeg-turbo) -> JPEG bytes
  | WebSocket binary frame
  v
browser: Blob -> createImageBitmap() (off-main-thread decode) -> <canvas>
```

## Data Flow: Input Interaction (Web)

```
browser: mousedown on canvas
  |
  | WebSocket: {"type":"press","x":100,"y":200}
  v
lvv WSHandler: protocol_->press(100, 200)
  |
  | TCP: {"cmd":"press","x":100,"y":200}
  v
spy: inject_press(100, 200)
  -> sets indev state
  -> lv_timer_ready() + lv_timer_handler()
  -> LVGL processes the touch event
```

## Data Flow: Test Execution

```
CLI: lvv run test_login.py
  |
  v
TestRunner::run_file("test_login.py")
  -> ScriptEngine::run_string(code)
     -> PocketPy executes on dedicated thread
     -> import lvv  (lvv_module bindings)
     -> lvv.click("btn_login")
        -> Protocol::click("btn_login")
           -> Transport::send({"cmd":"click","name":"btn_login"})
  |
  v
TestResult { status=Pass/Fail, output, duration }
  -> JUnit XML report
```
