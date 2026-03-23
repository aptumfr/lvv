import lvv

# Test: Tree snapshot — structural comparison
# Target: demo_with_spy

# Full tree structure check (first run creates reference)
lvv.assert_tree("full_tree.json")

# Subtree check — only the home screen
lvv.assert_tree("home_tree.json", "home_screen")

# Navigate to settings and check that subtree
lvv.click("btn_settings")
lvv.wait_for("settings_screen", 2000)
lvv.assert_tree("settings_tree.json", "settings_screen")

# Check settings layout with geometry (5px tolerance)
lvv.assert_tree("settings_layout.json", "settings_screen", True, 5)

# Go back
lvv.click("btn_back_settings")
lvv.wait_for("home_screen", 2000)

# Verify full tree is unchanged after round-trip
lvv.assert_tree("full_tree.json")

print("Tree snapshot tests passed!")
