import lvv

# Test: Login flow
# Assumes an LVGL UI with a login screen

# Verify the login screen is visible
lvv.assert_visible("login_screen")

# Type username
lvv.click("input_username")
lvv.type_text("admin")

# Type password
lvv.click("input_password")
lvv.type_text("secret123")

# Click login button
lvv.click("button[Login]")

# Wait for transition
lvv.wait(500)

# Verify we reached the dashboard
lvv.assert_visible("dashboard_screen")
lvv.assert_hidden("login_screen")

# Take a screenshot for visual regression
lvv.screenshot_compare("ref_images/dashboard.png", 0.1)

print("Login test passed!")
