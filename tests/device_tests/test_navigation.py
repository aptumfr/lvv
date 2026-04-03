import lvv
import json

# Test: Navigation demo on real device
# Target: navigation demo app over serial
# Run: lvv --serial /dev/ttyACM2 --baud 230400 run tests/device_tests/test_navigation.py
#
# Note: uses click_at() + wait() instead of click(name) + sync() because
# widgets on this target have no names set. Once the spy is updated with
# sync support and widgets are named, replace waits with sync() for
# faster, deterministic execution.

# --- Ensure we're on the home screen ---
# Try clicking back button repeatedly to return home from any screen
for i in range(5):
    home = lvv.find_by("type=label,text=Home")
    if home is not None:
        break
    # Click back button area (top-left)
    lvv.click_at(68, 63)
    lvv.wait(500)

# --- Home screen ---

home = lvv.find_by("type=label,text=Home")
assert home is not None, "Home title not found — not on home screen"

settings_row = lvv.find_by("type=label,text=Settings")
assert settings_row is not None, "Settings button not found"

profile_row = lvv.find_by("type=label,text=Profile")
assert profile_row is not None, "Profile button not found"

about_row = lvv.find_by("type=label,text=About")
assert about_row is not None, "About button not found"

print("Home screen verified")

# --- Navigate to Settings ---

s = json.loads(settings_row)
lvv.click_at(s["x"] + s["width"] // 2, s["y"] + s["height"] // 2)
lvv.wait(500)

# Verify Settings screen by checking for switches and sliders
switches = json.loads(lvv.find_all_by("type=switch,visible=true"))
assert len(switches) >= 2, "Expected at least 2 switches on Settings screen"

sliders = json.loads(lvv.find_all_by("type=slider,visible=true"))
assert len(sliders) >= 2, "Expected at least 2 sliders on Settings screen"

brightness = lvv.find_by("type=label,text=Brightness")
assert brightness is not None, "Brightness label not found"

print("Settings screen verified: " + str(len(switches)) + " switches, " + str(len(sliders)) + " sliders")

# --- Drag the first slider ---

first_slider = sliders[0]
cx = first_slider["x"] + first_slider["width"] // 4
cy = first_slider["y"] + first_slider["height"] // 2
lvv.drag(cx, cy, cx + first_slider["width"] // 2, cy, 500)
lvv.wait(200)

print("Slider dragged")

# --- Go back to home (back button is a button widget in top area) ---

back_btn = lvv.find_by("type=button")
assert back_btn is not None, "Back button not found"
b = json.loads(back_btn)
lvv.click_at(b["x"] + b["width"] // 2, b["y"] + b["height"] // 2)
lvv.wait(500)

home_check = lvv.find_by("type=label,text=Home")
assert home_check is not None, "Did not return to home screen"

print("Back to home verified")

# --- Navigate to Profile ---

p = json.loads(profile_row)
lvv.click_at(p["x"] + p["width"] // 2, p["y"] + p["height"] // 2)
lvv.wait(500)

profile_title = lvv.find_by("type=label,text=Profile")
assert profile_title is not None, "Profile screen did not open"

print("Profile screen verified")

# Go back
back_btn = lvv.find_by("type=button")
b = json.loads(back_btn)
lvv.click_at(b["x"] + b["width"] // 2, b["y"] + b["height"] // 2)
lvv.wait(500)

# --- Navigate to About ---

a = json.loads(about_row)
lvv.click_at(a["x"] + a["width"] // 2, a["y"] + a["height"] // 2)
lvv.wait(500)

about_title = lvv.find_by("type=label,text=About")
assert about_title is not None, "About screen did not open"

print("About screen verified")

# Go back
back_btn = lvv.find_by("type=button")
b = json.loads(back_btn)
lvv.click_at(b["x"] + b["width"] // 2, b["y"] + b["height"] // 2)
lvv.wait(500)

# --- Final check: back on home ---

final_home = lvv.find_by("type=label,text=Home")
assert final_home is not None, "Did not return to home after full navigation"

# Visual regression: save screenshot of home screen
lvv.screenshot_compare("device_home.png", 0.5)

print("Device navigation test passed!")
