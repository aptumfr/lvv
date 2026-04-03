import lvv
import json

# Test: Deterministic test execution using sync() and verified click()
# Target: demo_with_spy / demo_headless
# Exercises: sync barrier, click interception detection, state assertions

# --- Verify sync() works: click then assert without timing guesswork ---

lvv.click("btn_settings")
lvv.sync()
lvv.assert_visible("settings_screen")
lvv.assert_visible("settings_title")

print("sync after navigation: OK")

# --- Verify sync() after going back ---

lvv.click("btn_back_settings")
lvv.sync()
lvv.assert_visible("home_screen")
lvv.assert_visible("home_title")

print("sync after back: OK")

# --- Verify click interception detection ---
# Open a dialog, then try to click a button behind it

lvv.click("btn_dialog")
lvv.sync()

# The dialog should be visible
dialog = lvv.find_with_retry("name=confirm_dialog", 2000)
assert dialog is not None, "Dialog did not appear"

# Try clicking btn_settings which is behind the dialog — should raise AssertionError
caught = False
try:
    lvv.click("btn_settings")
except AssertionError:
    caught = True

assert caught, "Click behind dialog should have raised AssertionError"

print("click interception detected: OK")

# Dismiss the dialog properly
lvv.click("btn_no")
lvv.sync()

# Verify dialog is gone
gone = lvv.find("confirm_dialog")
assert gone is None, "Dialog should have been dismissed"

print("dialog dismissed: OK")

# --- Verify sync() with slider interaction ---

lvv.click("btn_settings")
lvv.sync()

# Read brightness value before drag
before = json.loads(lvv.find("brightness_value"))
before_text = before["text"]

x, y, w, h = lvv.widget_coords("brightness_slider")
lvv.drag(x + 5, y + h // 2, x + w - 5, y + h // 2, 400)
lvv.sync()

# Read brightness value after drag — should have changed
after = json.loads(lvv.find("brightness_value"))
after_text = after["text"]
assert before_text != after_text, "Brightness value did not change after drag: " + before_text

print("sync after drag: OK (" + before_text + " -> " + after_text + ")")

# --- Go back and verify final state ---

lvv.click("btn_back_settings")
lvv.sync()
lvv.assert_visible("home_screen")

print("Determinism test passed!")
