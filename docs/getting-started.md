# Getting Started

## Building

### Requirements

- C++20 compiler (GCC 11+, Clang 14+)
- CMake 3.20+
- libjpeg-dev (libjpeg-turbo recommended)
- Node.js 18+ (for web UI, optional)

On Ubuntu/Debian:

```bash
sudo apt install build-essential cmake libjpeg-turbo8-dev
```

### Build LVV

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

For a release build (smaller binary, optimized):

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Build the Web UI

```bash
cd web
npm install
npm run build
```

The build output goes to `web/dist/`, which the server serves automatically.

## Integrating the Spy

Add `spy/lvv_spy.c` and `spy/lvv_spy.h` to your LVGL application.

```c
#include "lvv_spy.h"

int main(void) {
    // ... LVGL init ...

    lvv_spy_init(5555);  // Start spy on port 5555

    while (1) {
        lv_timer_handler();
        lvv_spy_process();  // Process spy commands (non-blocking)
        lv_delay_ms(5);
    }

    lvv_spy_deinit();
}
```

Requirements in `lv_conf.h`:
- `LV_USE_OBJ_NAME 1` (optional, enables named widgets for easier test selectors)

Note: screenshots use LVGL's internal draw pipeline directly — no `LV_USE_SNAPSHOT` needed.

### Naming Widgets

For reliable test selectors, name your widgets:

```c
lv_obj_t* btn = lv_button_create(parent);
lv_obj_set_name(btn, "btn_login");
```

Without names, LVV generates auto-paths like `button[Login]` (by text) or `slider[0]` (by index).

## Usage

### Interactive Mode (Web UI)

```bash
./lvv serve --host localhost --port 5555 --web-port 8080
```

Open http://localhost:8080 in a browser. You get:
- Live view with mouse interaction
- Widget tree browser
- Test script editor with recording
- Visual regression comparison

### One-Shot Commands

```bash
# Check connection
./lvv ping --host localhost --port 5555

# Dump widget tree
./lvv tree --host localhost --port 5555

# Take a screenshot
./lvv screenshot -o screen.png --host localhost --port 5555
```

### Running Tests

```bash
# Run test files
./lvv run tests/ --host localhost --port 5555

# With JUnit output for CI
./lvv run tests/ --output results.xml --timeout 30

# With visual regression baselines
./lvv run tests/ --ref-images ref_images/ --threshold 0.1
```

### Serial Connection

```bash
./lvv serve --serial /dev/ttyUSB0 --baud 115200
./lvv run tests/ --serial /dev/ttyUSB0 --baud 921600
```

## Writing Tests

Tests are Python scripts using the `lvv` module:

```python
import lvv

# Click a named widget
lvv.click("btn_login")

# Wait for a widget to appear
lvv.wait_for("dashboard", 5000)

# Assert widget state
lvv.assert_visible("welcome_label")
lvv.assert_value("count_label", "text", "42")

# Visual regression
assert lvv.screenshot_compare("login_screen.png", 0.1)

# Input
lvv.type_text("user@example.com")
lvv.key("ENTER")

# Low-level touch
lvv.press(100, 200)
lvv.wait(100)
lvv.move_to(300, 200)
lvv.release()
```

See [scripting-api.md](scripting-api.md) for the full API reference.
