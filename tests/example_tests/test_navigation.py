import lvv

# Test: Navigation between screens

# Start on dashboard
lvv.assert_visible("dashboard_screen")

# Navigate to settings via menu
lvv.click("menu_btn")
lvv.wait(200)
lvv.click("button[Settings]")
lvv.wait(300)

# Verify settings screen
lvv.assert_visible("settings_screen")

# Change a slider value
lvv.click("slider[Brightness]")

# Swipe the slider to the right
info = lvv.find("slider[Brightness]")
# Swipe across the slider
lvv.swipe(100, 200, 300, 200, 500)

# Go back
lvv.click("button[Back]")
lvv.wait(300)

# Verify we're back on dashboard
lvv.assert_visible("dashboard_screen")

print("Navigation test passed!")
