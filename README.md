# LVV - LVGL Test Automation Tool

LVV is a test automation tool for [LVGL](https://lvgl.io/) applications. It connects to an LVGL target (running the LVV Spy), provides widget inspection, interaction, visual regression testing, and a web UI for interactive debugging.

## Architecture

```
+------------------+          TCP           +-------------------+
|       lvv        | <--------------------> |   LVGL App        |
|  (host machine)  |     (port 5555)        |   + lvv_spy       |
+------------------+                        +-------------------+
  - CLI commands
  - Web UI (port 8080)
  - Test runner (PocketPy)
```

**lvv** runs on the host. It connects to the target over TCP (or serial) and speaks the spy protocol to inspect widgets, send input events, and capture screenshots.

**lvv_spy** is a small C library embedded in your LVGL application. It listens for connections and handles commands.

## Building

Requirements: C++20 compiler, CMake 3.20+, pthreads.

```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

The `lvv` binary is in `build/lvv`.

## Target Setup

Add the spy to your LVGL application:

```c
#include "lvv_spy.h"

int main() {
    lv_init();
    // ... create display, build UI ...

    lvv_spy_init(5555);  // Start spy on TCP port 5555

    while (true) {
        lv_timer_handler();
        lvv_spy_process();  // Non-blocking
        usleep(5000);
    }
}
```

With the lv:: C++ wrapper:

```cpp
#include <lv/lv.hpp>
extern "C" { #include "lvv_spy.h" }

int main() {
    lv::init();
    // ... create display, build UI ...
    lvv_spy_init(5555);
    lv::run_with([&]() { lvv_spy_process(); return true; });
}
```

Link against `lvv_spy` and `lvgl`. See `example/CMakeLists.txt` for a complete build setup.

## Environment Variables

| Variable | Used by | Description |
|---|---|---|
| `LVGL_DIR` | example CMake | Path to LVGL repo (default: `../../lvgl`) |
| `LV_CPP_DIR` | example CMake | Path to lv:: C++ wrapper repo (default: `../../lv`) |
| `LVV_HOST` | `lvv` CLI | Default target host (overridden by `--host`) |
| `LVV_PORT` | `lvv` CLI | Default target port (overridden by `--port`) |

CMake variables can also be set directly: `cmake .. -DLVGL_DIR=/path/to/lvgl`.

## Usage

LVV has five subcommands: `ping`, `tree`, `screenshot`, `run`, and `serve`.

### Global Options

These apply to all subcommands:

| Option | Default | Description |
|---|---|---|
| `--host HOST` | `localhost` | Target hostname or IP |
| `--port PORT` | `5555` | Target TCP port |
| `--serial DEVICE` | | Serial device (e.g. `/dev/ttyUSB0`) |
| `--baud RATE` | `115200` | Serial baud rate |
| `-v, --verbose` | off | Debug logging |

### ping

Check connectivity to the target.

```bash
lvv ping
lvv --port 5556 ping
lvv --host 192.168.1.100 ping
```

### tree

Print the widget tree from the target.

```bash
lvv tree
lvv tree --auto-paths    # Show auto_path for each widget
```

| Option | Description |
|---|---|
| `-a, --auto-paths` | Show the auto_path for each widget |

### screenshot

Capture the current screen as a PNG file.

```bash
lvv screenshot
lvv screenshot -o my_capture.png
```

| Option | Default | Description |
|---|---|---|
| `-o, --output FILE` | `screenshot.png` | Output file path |

### run

Run test scripts (PocketPy Python). This is the main mode for CI and automated testing.

```bash
# Single test
lvv run tests/test_navigation.py

# Multiple tests
lvv run tests/test_navigation.py tests/test_settings.py

# Whole directory
lvv run tests/

# With JUnit XML output (for CI)
lvv run --output results.xml tests/

# Custom settings
lvv run --timeout 60 --threshold 0.5 --ref-images my_refs/ tests/
```

| Option | Default | Description |
|---|---|---|
| `--output FILE` | | JUnit XML output file |
| `--ref-images DIR` | `ref_images` | Directory for visual regression reference images |
| `--threshold PCT` | `0.1` | Visual diff threshold (0-100%) |
| `--timeout SECS` | `30` | Per-test timeout in seconds |

Test scripts use the `lvv` Python module. See [docs/scripting-api.md](docs/scripting-api.md) for the full API reference.

### serve

Start the web UI for interactive debugging, live screen viewing, and running tests from the browser.

```bash
lvv serve
lvv --port 5555 serve --web-port 9090
```

Then open `http://localhost:8080` in a browser.

| Option | Default | Description |
|---|---|---|
| `--web-port PORT` | `8080` | Web server port |
| `--static-dir DIR` | auto-detected | Directory with the React build |
| `--ref-images DIR` | `ref_images` | Reference images directory |
| `--threshold PCT` | `0.1` | Visual diff threshold |
| `--timeout SECS` | `30` | Per-test timeout |

The web UI provides:
- Live screenshot stream
- Interactive widget tree explorer
- Click/drag/type directly on the screen
- Run test scripts and view results

The web server also exposes a REST API and WebSocket. See [docs/api.md](docs/api.md) for details.

## Connection Types

**TCP (default):** Target and host communicate over a TCP socket. The spy listens on the port passed to `lvv_spy_init()`.

```bash
lvv --host 192.168.1.100 --port 5555 tree
```

**Serial:** For targets without networking (e.g. bare-metal MCUs). The spy communicates over UART.

```bash
lvv --serial /dev/ttyUSB0 --baud 115200 tree
```

## Writing Tests

Tests are Python scripts using the `lvv` module:

```python
import lvv
import json

# Wait for a screen to appear
lvv.wait_for("home_screen", 3000)

# Click a button
lvv.click("btn_settings")
lvv.wait_for("settings_screen", 2000)

# Find and drag a slider
s = json.loads(lvv.find("brightness_slider"))
cy = s["y"] + s["height"] // 2
lvv.drag(s["x"] + 5, cy, s["x"] + s["width"] - 5, cy, 400)

# Assert state
lvv.assert_visible("settings_title")

# Visual regression
lvv.screenshot_compare("settings.png", 0.5)

# Navigate back
lvv.click("btn_back")
lvv.wait_for("home_screen", 2000)
```

Use selectors to find widgets by property:

```python
btn = lvv.find_by("type=button,text=OK")
all_btns = lvv.find_all_by("type=button,visible=true")
```

See [docs/scripting-api.md](docs/scripting-api.md) for the complete Python API reference.

## Example Apps

The `example/` directory contains sample LVGL apps with the spy enabled:

- **counter_with_spy** - Simple counter with 3 buttons
- **demo_with_spy** - Multi-screen app with settings, list, and dialog screens
- **demo_headless** - Same demo, no display required (for CI)

Build them separately:

```bash
cd example
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)

# Run the demo (needs X11 or SDL)
./demo_with_spy

# Or run headless (no display needed)
./demo_headless
```

Then in another terminal:

```bash
lvv --port 5555 tree
lvv --port 5555 run tests/example_tests/test_navigation.py
lvv --port 5555 serve
```

## Headless CI

Run tests in Docker or CI without any display server. The headless driver creates an
LVGL display with no output — the spy's screenshot command still works using LVGL's
internal draw engine.

Add to your LVGL app:

```c
#include "lvv_headless.h"
#include "lvv_spy.h"

int main() {
    lv_init();
    lvv_headless_create(800, 480);
    build_ui();
    lvv_spy_init(5555);
    lvv_headless_run();  // blocks, runs LVGL + spy loop
}
```

Or compile an existing app with `-DLVV_HEADLESS` to switch display backends.

**Docker** (run from the parent directory containing `lvgl/`, `lv/`, and `lv2/`):

```bash
docker build -f lv2/Dockerfile.ci -t lvv-ci .
docker run --rm lvv-ci
```

**GitHub Actions:** See `.github/workflows/ci.yml` for a ready-made workflow.

## Documentation

| File | Contents |
|---|---|
| [docs/scripting-api.md](docs/scripting-api.md) | Python `lvv` module API reference |
| [docs/api.md](docs/api.md) | HTTP REST and WebSocket API reference |
| [docs/spy-protocol.md](docs/spy-protocol.md) | Wire protocol between lvv and target |
| [docs/architecture.md](docs/architecture.md) | System architecture overview |
| [docs/getting-started.md](docs/getting-started.md) | Build and setup guide |
