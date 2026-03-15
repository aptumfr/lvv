/**
 * @file counter_with_spy.cpp
 * @brief Counter example with LVV Spy enabled for remote test automation
 *
 * This is the lv:: counter example with the spy library added.
 * Run it, then connect with:
 *   lvv ping --port 5555
 *   lvv tree --port 5555
 *   lvv serve --port 5555
 */

#include <lv/lv.hpp>
#include <lv/assets/cursor.hpp>

extern "C" {
#include "lvv_spy.h"
}

#if LV_USE_OBSERVER

class CounterApp : public lv::Component<CounterApp> {
    lv::State<int> m_count{0};

public:
    lv::ObjectView build(lv::ObjectView parent) {
        auto root = lv::vbox(parent)
            .fill()
            .padding(20)
            .gap(16)
            .center_content()
            .name("counter_root");

        lv::Label::create(root)
            .text("Counter Example")
            .font(lv::fonts::montserrat_20)
            .center_text()
            .name("title_label");

        lv::Label::create(root)
            .bind_text(m_count, "Count: %d")
            .font(lv::fonts::montserrat_28)
            .center_text()
            .name("count_label");

        auto buttons = lv::hbox(root)
            .gap(20)
            .size_content()
            .name("button_row");

        lv::Button::create(buttons)
            .size(60, 60)
            .radius(10)
            .text("-")
            .on_click<&CounterApp::decrement>(this)
            .name("btn_minus");

        lv::Button::create(buttons)
            .size(80, 60)
            .radius(10)
            .text("Reset")
            .on_click<&CounterApp::reset>(this)
            .name("btn_reset");

        lv::Button::create(buttons)
            .size(60, 60)
            .radius(10)
            .text("+")
            .on_click<&CounterApp::increment>(this)
            .name("btn_plus");

        return root;
    }

private:
    void increment(lv::Event) { ++m_count; }
    void decrement(lv::Event) { --m_count; }
    void reset(lv::Event) { m_count.set(0); }
};

#endif

int main() {
    lv::init();

#if LV_USE_X11
    lv::X11Display display("Counter + LVV Spy", 800, 480, &lv::cursor_arrow);
#elif LV_USE_SDL
    lv::SDLDisplay display(800, 480);
#else
    #error "No display backend enabled"
#endif

#if LV_USE_OBSERVER
    CounterApp app;
    app.mount(lv::screen_active());
#else
    lv::Label::create(lv::screen_active())
        .text("Enable LV_USE_OBSERVER")
        .center();
#endif

    // Initialize the spy server
    if (!lvv_spy_init(5555)) {
        return 1;
    }

    // Run with spy processing in the loop
    lv::run_with([]() {
        lvv_spy_process();
        return true;
    });
}
