import lvv
import json

# Test: Long press and drag gestures
# Target: demo_with_spy

# Navigate to settings
lvv.click("btn_settings")
lvv.wait_for("settings_screen", 2000)

# --- Drag the brightness slider ---
slider = lvv.find("brightness_slider")
assert slider is not None, "Brightness slider not found"

s = json.loads(slider)
sx = s["x"]
sy = s["y"]
sw = s["width"]
sh = s["height"]

# Drag from center-left to center-right (increase brightness)
center_y = sy + sh // 2
lvv.drag(sx + 10, center_y, sx + sw - 10, center_y, 500)
lvv.wait(200)

# --- Drag the volume slider ---
vol_slider = lvv.find("volume_slider")
assert vol_slider is not None, "Volume slider not found"
vs = json.loads(vol_slider)

# Drag to reduce volume (right to left)
vy = vs["y"] + vs["height"] // 2
lvv.drag(vs["x"] + vs["width"] - 10, vy, vs["x"] + 10, vy, 500)
lvv.wait(200)

# --- Long press on a button ---
back_btn = lvv.find("btn_back_settings")
assert back_btn is not None, "Back button not found"
b = json.loads(back_btn)
bx = b["x"] + b["width"] // 2
btn_y = b["y"] + b["height"] // 2

lvv.long_press(bx, btn_y, 800)
lvv.wait(200)

# We should be back on home after the long press triggered the click
lvv.wait_for("home_screen", 2000)

print("Gesture tests passed!")
