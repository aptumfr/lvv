import lvv

# Test: Navigation between screens
# Target: demo_with_spy

# Start on home
lvv.assert_visible("home_screen")

# Navigate to settings
lvv.click("btn_settings")
lvv.wait_for("settings_screen", 2000)
lvv.assert_visible("settings_title")

# Go back
lvv.click("btn_back_settings")
lvv.wait_for("home_screen", 2000)

# Navigate to list
lvv.click("btn_items")
lvv.wait_for("list_screen", 2000)
lvv.assert_visible("list_title")

# Go back
lvv.click("btn_back_list")
lvv.wait_for("home_screen", 2000)

# Verify we're back on home
lvv.assert_visible("home_title")

# Take a screenshot of home for visual regression
lvv.screenshot_compare("home_screen.png", 0.1)

print("Navigation test passed!")
