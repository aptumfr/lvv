import lvv
import json

# Test: List scrolling and item interaction
# Target: demo_with_spy

# Navigate to list screen
lvv.click("btn_items")
lvv.wait_for("list_screen", 2000)

# Verify list title
lvv.assert_visible("list_title")

# Find the first item
item0 = lvv.find("item_0")
assert item0 is not None, "First list item not found"

# Click the first item
lvv.click("item_0")
lvv.wait(200)

# Scroll the list down using drag (swipe up on the list)
list_widget = lvv.find("item_list")
assert list_widget is not None, "List widget not found"
lw = json.loads(list_widget)

# Drag from bottom of list to top (scroll down)
lx = lw["x"] + lw["width"] // 2
ly_start = lw["y"] + lw["height"] - 20
ly_end = lw["y"] + 20
lvv.drag(lx, ly_start, lx, ly_end, 400)
lvv.wait(300)

# Long press on a list item (simulates context menu trigger)
item3 = lvv.find("item_3")
if item3 is not None:
    i3 = json.loads(item3)
    lvv.long_press(
        i3["x"] + i3["width"] // 2,
        i3["y"] + i3["height"] // 2,
        600
    )
    lvv.wait(200)

# Go back
lvv.click("btn_back_list")
lvv.wait_for("home_screen", 2000)

print("List scroll tests passed!")
