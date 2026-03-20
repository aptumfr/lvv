import lvv

# Start on home screen
lvv.assert_visible("home_screen")

# Open dialog and dismiss it
lvv.click("btn_dialog")
lvv.wait_for("confirm_dialog", 2000)
assert lvv.click("btn_no"), "btn_no click failed"
lvv.wait(300)
assert lvv.find("confirm_dialog") is None, "Dialog should have been dismissed"

# Navigate to items list
lvv.assert_visible("home_screen")
lvv.click("btn_items")
lvv.wait_for("list_screen", 2000)

# Scroll the list
import json
list_w = lvv.find("item_list")
assert list_w is not None, "Item list not found"
lw = json.loads(list_w)
lx = lw["x"] + lw["width"] // 2
lvv.drag(lx, lw["y"] + lw["height"] - 20, lx, lw["y"] + 20, 600)
lvv.wait(500)

# Go back to home
lvv.click("btn_back_list")
lvv.wait_for("home_screen", 2000)

# Navigate to settings
lvv.click("btn_settings")
lvv.wait_for("settings_screen", 2000)

# Drag brightness slider
slider = lvv.find("brightness_slider")
assert slider is not None, "Brightness slider not found"
s = json.loads(slider)
cy = s["y"] + s["height"] // 2
lvv.drag(s["x"] + s["width"] - 10, cy, s["x"] + s["width"] // 2, cy, 800)
lvv.wait(300)

# Toggle dark mode checkbox
lvv.click("dark_mode_check")
lvv.wait(300)

# Go back to home
lvv.click("btn_back_settings")
lvv.wait_for("home_screen", 2000)

lvv.assert_visible("home_screen")
print("Recorded test passed!")
