# LVV Python Test Scripting Reference

Test scripts are written in Python (PocketPy) and have access to the `lvv` module.
PocketPy includes `json` and `math` from the standard library.

```python
import lvv
import json
```

---

## Finding Widgets

### `lvv.find(name) -> str | None`

Find a widget by its name. Returns a JSON string with widget info, or `None` if not found.

```python
btn = lvv.find("btn_ok")
if btn is not None:
    info = json.loads(btn)
    print(info["x"], info["y"], info["width"], info["height"])
```

Returned JSON fields: `name`, `type`, `x`, `y`, `width`, `height`, `visible`, `clickable`, `auto_path`, `text`.

### `lvv.find_by(selector) -> str | None`

Find the first widget matching a multi-property selector. Returns JSON string or `None`.

Selector syntax: comma-separated `key=value` pairs. All must match.

Supported keys: `type`, `name`, `text`, `visible`, `clickable`, `auto_path`.

Boolean keys (`visible`, `clickable`) accept `true`, `false`, `1`, or `0`. Other values raise `ValueError`.

Values may contain commas and `=` characters — the parser only splits on `,` when followed by a recognized key. For example, `text=Hello, world,type=label` correctly parses as two pairs.

**Caveat:** A typo in a key name after a valid clause (e.g. `type=button,tyep=label`) will be absorbed into the preceding value rather than raising an error. The selector won't match any widget, but no error is reported. To catch typos early, put commonly mistyped clauses first — unknown keys at the start of a selector are always rejected.

```python
btn = lvv.find_by("type=button,text=OK")
btn = lvv.find_by("type=slider,visible=true")
btn = lvv.find_by("name=home_title")
btn = lvv.find_by("text=Price: $1,500,type=label")  # comma in text value
```

### `lvv.find_all_by(selector) -> str`

Find all widgets matching a selector. Returns a JSON array string (may be empty `"[]"`).

```python
buttons = lvv.find_all_by("type=button,visible=true")
items = json.loads(buttons)
print("Found", len(items), "buttons")
```

### `lvv.find_at(x, y) -> str | None`

Find the deepest widget at screen coordinates. Returns JSON string or `None`.

```python
widget = lvv.find_at(150, 200)
```

### `lvv.find_with_retry(name_or_selector, timeout_ms) -> str`

Poll for a widget until found or timeout. Supports both names and selectors
(auto-detected by presence of `=`). Polls every 100ms.

Raises `TimeoutError` if not found within the timeout.

```python
# By name
home = lvv.find_with_retry("home_title", 3000)

# By selector
btn = lvv.find_with_retry("type=button,text=Settings", 3000)
```

### `lvv.widget_coords(name) -> tuple`

Get the position and size of a widget as `(x, y, width, height)`.
Raises `RuntimeError` if the widget is not found.

```python
x, y, w, h = lvv.widget_coords("brightness_slider")
lvv.drag(x + 5, y + h // 2, x + w - 5, y + h // 2, 400)
```

### `lvv.get_all_widgets() -> str`

Get a flat list of all widgets in the tree. Returns a JSON array string.

```python
all_w = json.loads(lvv.get_all_widgets())
for w in all_w:
    print(w["name"], w["type"])
```

### `lvv.get_tree() -> str`

Get the full widget tree hierarchy as a JSON string.

### `lvv.get_props(name) -> str`

Get all properties of a widget by name. Returns a JSON string with target-dependent properties.

### `lvv.screen_info() -> str`

Get display dimensions and color format.

```python
info = json.loads(lvv.screen_info())
print(info["width"], info["height"], info["color_format"])
```

---

## Interaction

### `lvv.click(name) -> bool`

Click a widget by name.

```python
lvv.click("btn_ok")
```

### `lvv.click_at(x, y) -> bool`

Click at screen coordinates.

```python
lvv.click_at(150, 200)
```

### `lvv.press(x, y) -> bool`

Begin a pointer press at coordinates.

### `lvv.release() -> bool`

Release the pointer.

### `lvv.move_to(x, y) -> bool`

Move pointer to coordinates (drag while pressed).

### `lvv.swipe(x1, y1, x2, y2, duration_ms) -> bool`

Swipe between two points.

```python
lvv.swipe(100, 200, 300, 200, 400)
```

### `lvv.long_press(x, y, duration_ms) -> bool`

Press, hold for duration, then release. Triggers `LV_EVENT_LONG_PRESSED`.

```python
widget = lvv.find("my_button")
w = json.loads(widget)
lvv.long_press(w["x"] + w["width"] // 2, w["y"] + w["height"] // 2, 800)
```

### `lvv.drag(x1, y1, x2, y2, duration_ms) -> bool`

Press at start, move in interpolated steps to end, then release.
Use for sliders, scrolling, list reordering.

```python
# Drag a slider from left to right
s = json.loads(lvv.find("brightness_slider"))
cy = s["y"] + s["height"] // 2
lvv.drag(s["x"] + 5, cy, s["x"] + s["width"] - 5, cy, 400)
```

### `lvv.type_text(text) -> bool`

Type text into the focused widget.

```python
lvv.click("input_field")
lvv.type_text("Hello World")
```

### `lvv.key(code) -> bool`

Send a key event.

Supported keys: `UP`, `DOWN`, `LEFT`, `RIGHT`, `ENTER`, `ESC`, `BACKSPACE`, `NEXT`, `PREV`.

```python
lvv.key("ENTER")
lvv.key("ESC")
```

---

## Waiting

### `lvv.wait(ms)`

Sleep for a duration in milliseconds. Supports cancellation.

```python
lvv.wait(300)
```

### `lvv.wait_for(name_or_selector, timeout_ms) -> bool`

Wait until a widget is visible. Supports both names and selectors.
Raises `TimeoutError` on failure.

```python
lvv.wait_for("settings_screen", 2000)
lvv.wait_for("name=settings_title", 3000)
```

### `lvv.wait_until(name, prop, value, timeout_ms) -> bool`

Wait until a widget property equals an expected value.
Raises `TimeoutError` on failure.

```python
lvv.wait_until("counter_label", "text", "5", 3000)
```

---

## Assertions

### `lvv.assert_visible(name)`

Assert that a widget exists and is visible. Raises `AssertionError` on failure.

```python
lvv.assert_visible("home_screen")
```

### `lvv.assert_hidden(name)`

Assert that a widget is either missing or hidden. Raises `AssertionError` on failure.

```python
lvv.assert_hidden("dialog_overlay")
```

### `lvv.assert_value(name, prop, expected)`

Assert that a widget property equals an expected string value.
Raises `AssertionError` on mismatch, `RuntimeError` if property doesn't exist.

```python
lvv.assert_value("status_label", "text", "Ready")
```

### `lvv.assert_range(name, prop, min, max)`

Assert that a numeric property is within a range (inclusive).
Raises `AssertionError` if out of range or not a number.

```python
lvv.assert_range("brightness_slider", "value", 0, 100)
lvv.assert_range("volume_slider", "value", 50, 80)
```

### `lvv.assert_match(name, prop, pattern)`

Assert that a property value matches a regex pattern (search, not full match).
Raises `AssertionError` on mismatch, `ValueError` for invalid regex.

```python
lvv.assert_match("status_label", "text", "^Ready")
lvv.assert_match("version_label", "text", r"\d+\.\d+\.\d+")
```

### `lvv.assert_true(name, prop)`

Assert that a boolean property is true (matches `"true"` or `"1"`).

```python
lvv.assert_true("wifi_switch", "checked")
lvv.assert_true("btn_ok", "clickable")
```

### `lvv.assert_false(name, prop)`

Assert that a boolean property is false (matches `"false"` or `"0"`).

```python
lvv.assert_false("wifi_switch", "checked")
lvv.assert_false("disabled_btn", "clickable")
```

---

## Visual Regression

### `lvv.screenshot(path) -> bool`

Save the current screen to a PNG file.

```python
lvv.screenshot("/tmp/current.png")
```

### `lvv.screenshot_compare(ref_path, threshold) -> bool`

Compare current screen against a reference image.
On first run (no reference), saves the current screen as the reference and returns `True`.

Relative paths are resolved against the `--ref-images` directory (default: `ref_images/`).

```python
lvv.screenshot_compare("home_screen.png", 0.1)
lvv.screenshot_compare("settings_configured.png", 0.5)
```

### `lvv.screenshot_compare_ex(ref_path, threshold, ignore_json) -> bool`

Like `screenshot_compare` but with ignore regions. The third argument is a JSON array
string of `[x, y, width, height]` rectangles to exclude from comparison.

```python
# Ignore a 100x30 timestamp area at top-right and a 50x50 animation at (200,100)
ignore = "[[700, 0, 100, 30], [200, 100, 50, 50]]"
lvv.screenshot_compare_ex("home.png", 0.1, ignore)
```

---

## Log Capture

Capture `LV_LOG` output from the target. Logs are stored in a ring buffer (64 entries max).

### `lvv.set_log_capture(enable) -> bool`

Enable or disable log capture on the target.

```python
lvv.set_log_capture(True)
# ... do things that produce log output ...
```

### `lvv.get_logs() -> str`

Get captured logs as a JSON string with a `"logs"` array.

```python
import json
lvv.set_log_capture(True)
lvv.click("btn_settings")
lvv.wait(500)
logs = json.loads(lvv.get_logs())
for entry in logs["logs"]:
    print(entry)
```

### `lvv.clear_logs() -> bool`

Clear the log buffer on the target.

---

## Performance Metrics

### `lvv.get_metrics() -> str`

Get performance metrics from the target as a JSON string.

```python
import json
metrics = json.loads(lvv.get_metrics())
print("Poll rate:", metrics["poll_rate"], "Hz")
print("Uptime:", metrics["uptime_ms"], "ms")
```

Returned fields: `poll_rate` (spy loop iterations per second), `uptime_ms`.

---

## Other

### `lvv.ping() -> str`

Ping the target. Returns the spy version string.

### `lvv.load_object_map(path) -> int`

Load a JSON object map (logical name -> physical name mapping).
Returns the number of entries loaded.

```python
lvv.load_object_map("object_map.json")
# Now lvv.click("login_button") resolves through the map
```

---

## Widget Type Names

The LVGL spy reports widget types as short names:

| LVGL Class | Type name in `lvv` |
|---|---|
| `lv_obj` | `obj` |
| `lv_btn` | `button` |
| `lv_label` | `label` |
| `lv_slider` | `slider` |
| `lv_switch` | `switch` |
| `lv_checkbox` | `checkbox` |
| `lv_dropdown` | `dropdown` |
| `lv_list` | `list` |
| `lv_msgbox` | `msgbox` |
| `lv_textarea` | `textarea` |

Use these short names in selectors: `type=button`, not `type=lv_btn`.

---

## Complete Example

```python
import lvv
import json

# Navigate to settings screen
lvv.click("btn_settings")
lvv.wait_for("settings_screen", 2000)

# Verify title is visible
lvv.assert_visible("settings_title")

# Drag brightness slider to max
slider = lvv.find("brightness_slider")
assert slider is not None, "Slider not found"
s = json.loads(slider)
cy = s["y"] + s["height"] // 2
lvv.drag(s["x"] + 5, cy, s["x"] + s["width"] - 5, cy, 400)
lvv.wait(200)

# Find all visible buttons
buttons = json.loads(lvv.find_all_by("type=button,visible=true"))
print("Found", len(buttons), "buttons")

# Visual check
lvv.screenshot_compare("settings_done.png", 0.5)

# Go back
lvv.click("btn_back_settings")
lvv.wait_for("home_screen", 2000)

print("Test passed!")
```
