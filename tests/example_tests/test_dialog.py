import lvv

# Test: Modal dialog interaction
# Target: demo_with_spy

# Verify home screen
lvv.assert_visible("home_screen")
lvv.assert_visible("status_label")

# Show dialog
lvv.click("btn_dialog")
lvv.wait(300)

# Wait for the dialog to appear using retry-aware find
dialog = lvv.find_with_retry("name=confirm_dialog", 3000)
assert dialog is not None, "Dialog did not appear"

# Find the Yes/No buttons using selectors
yes_btn = lvv.find_by("name=btn_yes")
assert yes_btn is not None, "Yes button not found in dialog"

no_btn = lvv.find_by("name=btn_no")
assert no_btn is not None, "No button not found in dialog"

# Take a screenshot with the dialog visible
lvv.screenshot_compare("dialog_open.png", 0.5)

# Click No to dismiss
lvv.click("btn_no")
lvv.wait(300)

# Dialog should be gone
gone = lvv.find("confirm_dialog")
assert gone is None, "Dialog should have been dismissed"

print("Dialog tests passed!")
