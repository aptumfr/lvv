import lvv

# Test: Retry-aware find and wait with selectors
# Target: demo_with_spy

# Home screen should be visible immediately
home = lvv.find_with_retry("home_title", 3000)
assert home is not None, "Home title not found within timeout"
print("Found home title: " + home)

# find_with_retry with multi-property selector
btn = lvv.find_with_retry("type=button,text=Settings", 3000)
assert btn is not None, "Settings button not found via selector retry"
print("Found settings button via retry: " + btn)

# Navigate and wait for a screen that takes time to appear
lvv.click("btn_settings")

# wait_for now supports selectors too
lvv.wait_for("name=settings_title", 3000)
print("Settings screen appeared (waited via selector)")

# Verify a specific widget is present using retry
slider = lvv.find_with_retry("type=slider,name=brightness_slider", 2000)
assert slider is not None, "Brightness slider not found via retry"

# Navigate to list screen
lvv.click("btn_back_settings")
lvv.wait_for("home_screen", 2000)

lvv.click("btn_items")
lvv.wait_for("list_screen", 2000)

# Find a list item using retry
item = lvv.find_with_retry("name=item_0", 3000)
assert item is not None, "First list item not found"
print("Found first list item: " + item)

# Find by selector in the list
all_btns = lvv.find_all_by("type=button,visible=true")
print("Visible buttons in list view: " + all_btns)

# Go back
lvv.click("btn_back_list")
lvv.wait_for("home_screen", 2000)

print("Retry find tests passed!")
