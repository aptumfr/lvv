/**
 * @file demo_with_spy.cpp
 * @brief Rich demo UI with LVV Spy for exercising all test automation features
 *
 * Creates a multi-screen app with various widget types:
 *   - Home screen: title, navigation buttons, status label
 *   - Settings screen: slider, checkbox, switch, dropdown
 *   - List screen: scrollable list with items
 *   - Dialog: modal popup triggered from home
 *
 * Run it, then connect with:
 *   lvv ping --port 5555
 *   lvv tree --port 5555
 *   lvv serve --port 5555
 */

#include <lv/lv.hpp>
#include <lv/assets/cursor.hpp>
#include <cstdio>

extern "C" {
#include "lvv_spy.h"
#ifdef LVV_HEADLESS
#include "lvv_headless.h"
#endif
}

#if LV_USE_OBSERVER

// ---- Settings Screen ----

class SettingsScreen : public lv::Component<SettingsScreen> {
    lv::State<int> m_brightness{50};
    lv::State<int> m_volume{75};
    lv::State<bool> m_wifi{true};

public:
    lv::ObjectView build(lv::ObjectView parent) {
        auto root = lv::vbox(parent)
            .fill()
            .padding(20)
            .gap(12)
            .name("settings_screen");

        lv::Label::create(root)
            .text("Settings")
            .font(lv::fonts::montserrat_20)
            .name("settings_title");

        // Brightness slider
        auto brightness_row = lv::hbox(root)
            .width(lv::pct(100))
            .height(LV_SIZE_CONTENT)
            .gap(10)
            .name("brightness_row");

        lv::Label::create(brightness_row)
            .text("Brightness")
            .name("brightness_label");

        lv::Slider::create(brightness_row)
            .width(lv::pct(60))
            .range(0, 100)
            .bind(m_brightness.subject())
            .name("brightness_slider");

        lv::Label::create(brightness_row)
            .bind_text(m_brightness, "%d%%")
            .name("brightness_value");

        // Volume slider
        auto volume_row = lv::hbox(root)
            .width(lv::pct(100))
            .height(LV_SIZE_CONTENT)
            .gap(10)
            .name("volume_row");

        lv::Label::create(volume_row)
            .text("Volume")
            .name("volume_label");

        lv::Slider::create(volume_row)
            .width(lv::pct(60))
            .range(0, 100)
            .bind(m_volume.subject())
            .name("volume_slider");

        lv::Label::create(volume_row)
            .bind_text(m_volume, "%d%%")
            .name("volume_value");

        // Wi-Fi switch
        auto wifi_row = lv::hbox(root)
            .width(lv::pct(100))
            .height(LV_SIZE_CONTENT)
            .gap(10)
            .name("wifi_row");

        lv::Label::create(wifi_row)
            .text("Wi-Fi")
            .name("wifi_label");

        lv::Switch::create(wifi_row)
            .bind(m_wifi.subject())
            .name("wifi_switch");

        // Dark mode checkbox (no state binding, just interactive)
        lv::Checkbox::create(root)
            .text("Dark Mode")
            .name("dark_mode_check");

        // Dropdown
        lv::Dropdown::create(root)
            .options("English\nFrench\nGerman\nSpanish\nArabic")
            .width(200)
            .name("language_dropdown");

        // Back button
        lv::Button::create(root)
            .size(100, 40)
            .text("Back")
            .on_click<&SettingsScreen::on_back>(this)
            .name("btn_back_settings");

        return root;
    }

private:
    void on_back(lv::Event) {
        lv_obj_add_flag(root().get(), LV_OBJ_FLAG_HIDDEN);
        auto home = lv_obj_get_child(lv_screen_active(), 0);
        if (home) lv_obj_remove_flag(home, LV_OBJ_FLAG_HIDDEN);
    }
};

// ---- List Screen ----

class ListScreen : public lv::Component<ListScreen> {
public:
    lv::ObjectView build(lv::ObjectView parent) {
        auto root = lv::vbox(parent)
            .fill()
            .padding(20)
            .gap(8)
            .name("list_screen");

        lv::Label::create(root)
            .text("Items")
            .font(lv::fonts::montserrat_20)
            .name("list_title");

        auto list = lv::List::create(root)
            .width(lv::pct(100))
            .grow(1)
            .name("item_list");

        // Add items
        const char* items[] = {
            "Alarm Clock", "Battery", "Camera", "Documents",
            "Email", "Favorites", "Gallery", "Health",
            "Internet", "Journal", "Keyboard", "Location"
        };
        for (int i = 0; i < 12; ++i) {
            char item_name[32];
            std::snprintf(item_name, sizeof(item_name), "item_%d", i);
            auto btn = list.add_button(LV_SYMBOL_FILE, items[i]);
            lv_obj_set_name(btn.get(), item_name);
        }

        // Status label
        lv::Label::create(root)
            .text("Select an item")
            .name("list_status");

        // Back button
        lv::Button::create(root)
            .size(100, 40)
            .text("Back")
            .on_click<&ListScreen::on_back>(this)
            .name("btn_back_list");

        return root;
    }

private:
    void on_back(lv::Event) {
        lv_obj_add_flag(root().get(), LV_OBJ_FLAG_HIDDEN);
        auto home = lv_obj_get_child(lv_screen_active(), 0);
        if (home) lv_obj_remove_flag(home, LV_OBJ_FLAG_HIDDEN);
    }
};

// ---- Home Screen ----

class HomeScreen : public lv::Component<HomeScreen> {
    SettingsScreen* m_settings;
    ListScreen* m_list;

public:
    HomeScreen(SettingsScreen* settings, ListScreen* list)
        : m_settings(settings), m_list(list) {}

    lv::ObjectView build(lv::ObjectView parent) {
        auto root = lv::vbox(parent)
            .fill()
            .padding(20)
            .gap(16)
            .center_content()
            .name("home_screen");

        lv::Label::create(root)
            .text("LVV Demo")
            .font(lv::fonts::montserrat_28)
            .center_text()
            .name("home_title");

        lv::Label::create(root)
            .text("Test Automation Demo App")
            .center_text()
            .name("home_subtitle");

        // Navigation buttons
        auto nav = lv::hbox(root)
            .gap(16)
            .size_content()
            .name("nav_row");

        lv::Button::create(nav)
            .size(120, 50)
            .text("Settings")
            .on_click<&HomeScreen::go_settings>(this)
            .name("btn_settings");

        lv::Button::create(nav)
            .size(120, 50)
            .text("Items")
            .on_click<&HomeScreen::go_list>(this)
            .name("btn_items");

        // Dialog button
        lv::Button::create(root)
            .size(160, 50)
            .text("Show Dialog")
            .on_click<&HomeScreen::show_dialog>(this)
            .name("btn_dialog");

        // Status
        lv::Label::create(root)
            .text("Ready")
            .name("status_label");

        return root;
    }

private:
    void go_settings(lv::Event) {
        lv_obj_add_flag(root().get(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(m_settings->root().get(), LV_OBJ_FLAG_HIDDEN);
    }

    void go_list(lv::Event) {
        lv_obj_add_flag(root().get(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(m_list->root().get(), LV_OBJ_FLAG_HIDDEN);
    }

    void show_dialog(lv::Event) {
        auto mbox = lv::Msgbox::create(lv::screen_active());
        (void)mbox.add_title("Confirm");
        (void)mbox.add_text("Are you sure you want to proceed?");
        mbox.name("confirm_dialog");

        auto close_cb = [](lv_event_t* e) {
            auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
            // Walk up to the msgbox parent and close it
            lv_obj_t* obj = btn;
            while (obj && !lv_obj_check_type(obj, &lv_msgbox_class)) {
                obj = lv_obj_get_parent(obj);
            }
            if (obj) lv_msgbox_close(obj);
        };

        auto yes_btn = mbox.add_footer_button("Yes");
        lv_obj_set_name(yes_btn.get(), "btn_yes");
        lv_obj_add_event_cb(yes_btn.get(), close_cb, LV_EVENT_CLICKED, nullptr);

        auto no_btn = mbox.add_footer_button("No");
        lv_obj_set_name(no_btn.get(), "btn_no");
        lv_obj_add_event_cb(no_btn.get(), close_cb, LV_EVENT_CLICKED, nullptr);

        (void)mbox.add_close_button();
    }
};

#endif // LV_USE_OBSERVER

int main() {
    lv::init();

#if defined(LVV_HEADLESS)
    // Headless mode for CI: no display hardware needed
    if (!lvv_headless_create(800, 480)) return 1;
#elif LV_USE_X11
    lv::X11Display display("LVV Demo + Spy", 800, 480, &lv::cursor_arrow);
#elif LV_USE_SDL
    lv::SDLDisplay display(800, 480);
#else
    #error "No display backend enabled (use -DLVV_HEADLESS for CI)"
#endif

#if LV_USE_OBSERVER
    auto screen = lv::screen_active();

    SettingsScreen settings;
    ListScreen list;
    HomeScreen home(&settings, &list);

    home.mount(screen);
    settings.mount(screen);
    list.mount(screen);

    // Start with only home visible
    lv_obj_add_flag(settings.root().get(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(list.root().get(), LV_OBJ_FLAG_HIDDEN);
#else
    lv::Label::create(lv::screen_active())
        .text("Enable LV_USE_OBSERVER")
        .center();
#endif

    if (!lvv_spy_init(5555)) {
        return 1;
    }

#if defined(LVV_HEADLESS)
    lvv_headless_run();
#else
    lv::run_with([]() {
        lvv_spy_process();
        return true;
    });
#endif
}
