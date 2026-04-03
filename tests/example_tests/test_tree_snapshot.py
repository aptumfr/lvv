import lvv

# Test: Tree snapshot — structural comparison
# Target: demo_with_spy
# Note: this test compares per-screen subtrees to avoid sensitivity
# to slider/checkbox state changes from other tests.

# Home screen subtree structure check
lvv.assert_tree("home_tree.json", "home_screen")

# Navigate to settings and check that subtree
lvv.click("btn_settings")
lvv.wait_for("settings_screen", 2000)
lvv.assert_tree("settings_tree.json", "settings_screen")

# Go back
lvv.click("btn_back_settings")
lvv.wait_for("home_screen", 2000)

# Verify home screen structure is unchanged after round-trip
lvv.assert_tree("home_tree.json", "home_screen")

print("Tree snapshot tests passed!")
