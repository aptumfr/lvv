import lvv
import json

# Test: Settings screen interactions (switch, checkbox, dropdown, sliders)
# Target: demo_with_spy

# Navigate to settings
lvv.click("btn_settings")
lvv.wait_for("settings_screen", 2000)

# --- Toggle Wi-Fi switch ---
wifi = lvv.find("wifi_switch")
assert wifi is not None, "Wi-Fi switch not found"

w = json.loads(wifi)
# Click the switch to toggle it off
lvv.click_at(w["x"] + w["width"] // 2, w["y"] + w["height"] // 2)
lvv.wait(200)

# --- Toggle dark mode checkbox ---
lvv.click("dark_mode_check")
lvv.wait(200)

# --- Select language from dropdown ---
lvv.click("language_dropdown")
lvv.wait(300)

# LVGL dropdowns render all options inside a single list obj,
# not as individual buttons. Find the dropdown list and click at the
# y-offset for the second item ("French").
dd = lvv.find("language_dropdown")
assert dd is not None, "Dropdown not found"
d = json.loads(dd)
# Click below the dropdown to select the second option (French)
# Each item is roughly 30px tall; second item center ~ y+45
lvv.click_at(d["x"] + d["width"] // 2, d["y"] + d["height"] + 45)
lvv.wait(200)

# --- Drag brightness slider to max ---
slider = lvv.find("brightness_slider")
assert slider is not None, "Brightness slider not found"
s = json.loads(slider)
cy = s["y"] + s["height"] // 2
lvv.drag(s["x"] + 5, cy, s["x"] + s["width"] - 5, cy, 400)
lvv.wait(200)

# Visual regression check of the settings screen
lvv.screenshot_compare("settings_configured.png", 0.5)

# Go back
lvv.click("btn_back_settings")
lvv.wait_for("home_screen", 2000)

print("Settings tests passed!")
