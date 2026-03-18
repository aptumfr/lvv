import lvv

# Test: Counter app basic interaction
# Target: counter_with_spy (not demo_with_spy)

# Skip if counter app is not running
if lvv.find("counter_root") is None:
    print("SKIP: counter_with_spy not running")
else:
    lvv.assert_visible("counter_root")
    lvv.assert_visible("count_label")

    # Click plus three times
    lvv.click("btn_plus")
    lvv.wait(100)
    lvv.click("btn_plus")
    lvv.wait(100)
    lvv.click("btn_plus")
    lvv.wait(100)

    # Click minus once
    lvv.click("btn_minus")
    lvv.wait(100)

    # Reset
    lvv.click("btn_reset")
    lvv.wait(100)

    # Visual regression
    lvv.screenshot_compare("counter_reset.png", 0.1)

    print("Counter test passed!")
