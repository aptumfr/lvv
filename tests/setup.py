import lvv

# Setup script: run before each test to ensure clean app state.
# Use with: lvv run --setup tests/example_tests/setup.py tests/example_tests/

# Dismiss any open dialogs
lvv.key("ESC")
lvv.wait(100)
lvv.key("ESC")
lvv.wait(100)

# Try navigating back to home via known back buttons
for btn in ["btn_back_settings", "btn_back_list"]:
    try:
        w = lvv.find(btn)
        if w is not None:
            lvv.click(btn)
            lvv.wait(200)
    except:
        pass

# Verify we're on the home screen (best effort)
try:
    lvv.wait_for("home_screen", 1000)
except:
    pass
