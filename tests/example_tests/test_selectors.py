import lvv

# Test: Multi-property selectors
# Target: demo_with_spy

# Find all visible buttons
buttons = lvv.find_all_by("type=button,visible=true")
print("Visible buttons: " + buttons)

# Find a specific button by type + text
settings_btn = lvv.find_by("type=button,text=Settings")
assert settings_btn is not None, "Settings button not found"
print("Found settings button: " + settings_btn)

# Find by name (equivalent to lvv.find but via selector syntax)
title = lvv.find_by("name=home_title")
assert title is not None, "Home title not found"
print("Title: " + title)

# Find label by text content
subtitle = lvv.find_by("type=label,text=Test Automation Demo App")
assert subtitle is not None, "Subtitle not found"

# Find all labels on the screen
labels = lvv.find_all_by("type=label,visible=true")
print("Visible labels: " + labels)

# Negative test: no match
missing = lvv.find_by("type=chart")
assert missing is None, "Should not find a chart widget"

# Find clickable widgets only
clickables = lvv.find_all_by("clickable=true,visible=true")
print("Clickable widgets: " + clickables)

print("Selector tests passed!")
