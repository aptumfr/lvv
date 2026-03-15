/**
 * @file lvv_spy.c
 * @brief LVV Spy - Embeddable LVGL introspection server
 *
 * Single-file implementation. Listens on a TCP port, accepts one client,
 * processes newline-delimited JSON commands, returns JSON responses.
 */

#include "lvv_spy.h"

#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* POSIX networking */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

/* ======================== Configuration ======================== */

#define LVV_VERSION       "0.1.0"
#define LVV_MAX_CMD_LEN   4096
#define LVV_MAX_RESP_LEN  (4 * 1024 * 1024)  /* 4MB for tree/screenshot */
#define LVV_MAX_TREE_DEPTH 32

/* ======================== State ======================== */

static int s_listen_fd = -1;
static int s_client_fd = -1;
static char s_cmd_buf[LVV_MAX_CMD_LEN];
static int s_cmd_len = 0;
static char* s_resp_buf = NULL;

/* ======================== Helpers ======================== */

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Simple JSON string append helpers (no external JSON library needed) */

static int resp_len;

static void resp_reset(void) {
    resp_len = 0;
    s_resp_buf[0] = '\0';
}

static void resp_append(const char* s) {
    int len = (int)strlen(s);
    if (resp_len + len < LVV_MAX_RESP_LEN) {
        memcpy(s_resp_buf + resp_len, s, len);
        resp_len += len;
        s_resp_buf[resp_len] = '\0';
    }
}

static void resp_append_int(int64_t val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", (long)val);
    resp_append(buf);
}

static void resp_append_str(const char* s) {
    resp_append("\"");
    /* Escape special characters */
    if (s) {
        for (const char* p = s; *p; p++) {
            switch (*p) {
                case '"':  resp_append("\\\""); break;
                case '\\': resp_append("\\\\"); break;
                case '\n': resp_append("\\n"); break;
                case '\r': resp_append("\\r"); break;
                case '\t': resp_append("\\t"); break;
                default: {
                    char c[2] = {*p, '\0'};
                    resp_append(c);
                }
            }
        }
    }
    resp_append("\"");
}

static void resp_append_bool(bool val) {
    resp_append(val ? "true" : "false");
}

/* ======================== JSON Parsing (minimal) ======================== */

/* Find a string value for a key in a JSON object (very simple parser) */
static bool json_get_string(const char* json, const char* key, char* out, int max_len) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) return false;

    pos += strlen(search);
    while (*pos == ' ' || *pos == ':') pos++;
    if (*pos != '"') return false;
    pos++;

    int i = 0;
    while (*pos && *pos != '"' && i < max_len - 1) {
        if (*pos == '\\' && *(pos + 1)) { pos++; }
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return true;
}

static bool json_get_int(const char* json, const char* key, int* out) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) return false;

    pos += strlen(search);
    while (*pos == ' ' || *pos == ':') pos++;

    char* end;
    long val = strtol(pos, &end, 10);
    if (end == pos) return false;
    *out = (int)val;
    return true;
}

/* ======================== Widget Type Names ======================== */

static const char* widget_type_name(lv_obj_t* obj) {
    const lv_obj_class_t* cls = lv_obj_get_class(obj);

    /* Check known LVGL classes */
    if (cls == &lv_obj_class)        return "obj";
    if (cls == &lv_label_class)      return "label";
    if (cls == &lv_button_class)     return "button";
#if LV_USE_IMAGE
    if (cls == &lv_image_class)      return "image";
#endif
#if LV_USE_SLIDER
    if (cls == &lv_slider_class)     return "slider";
#endif
#if LV_USE_SWITCH
    if (cls == &lv_switch_class)     return "switch";
#endif
#if LV_USE_CHECKBOX
    if (cls == &lv_checkbox_class)   return "checkbox";
#endif
#if LV_USE_DROPDOWN
    if (cls == &lv_dropdown_class)   return "dropdown";
#endif
#if LV_USE_ROLLER
    if (cls == &lv_roller_class)     return "roller";
#endif
#if LV_USE_TEXTAREA
    if (cls == &lv_textarea_class)   return "textarea";
#endif
#if LV_USE_ARC
    if (cls == &lv_arc_class)        return "arc";
#endif
#if LV_USE_BAR
    if (cls == &lv_bar_class)        return "bar";
#endif
#if LV_USE_SPINNER
    if (cls == &lv_spinner_class)    return "spinner";
#endif
#if LV_USE_LED
    if (cls == &lv_led_class)        return "led";
#endif
#if LV_USE_TABLE
    if (cls == &lv_table_class)      return "table";
#endif
#if LV_USE_CHART
    if (cls == &lv_chart_class)      return "chart";
#endif
#if LV_USE_CALENDAR
    if (cls == &lv_calendar_class)   return "calendar";
#endif
#if LV_USE_KEYBOARD
    if (cls == &lv_keyboard_class)   return "keyboard";
#endif
#if LV_USE_LIST
    if (cls == &lv_list_class)       return "list";
#endif
#if LV_USE_MENU
    if (cls == &lv_menu_class)       return "menu";
#endif
#if LV_USE_TABVIEW
    if (cls == &lv_tabview_class)    return "tabview";
#endif
#if LV_USE_TILEVIEW
    if (cls == &lv_tileview_class)   return "tileview";
#endif
#if LV_USE_MSGBOX
    if (cls == &lv_msgbox_class)     return "msgbox";
#endif
#if LV_USE_SPAN
    if (cls == &lv_spangroup_class)  return "spangroup";
#endif
#if LV_USE_SCALE
    if (cls == &lv_scale_class)      return "scale";
#endif
#if LV_USE_BUTTONMATRIX
    if (cls == &lv_buttonmatrix_class) return "buttonmatrix";
#endif
#if LV_USE_LINE
    if (cls == &lv_line_class)       return "line";
#endif
#if LV_USE_CANVAS
    if (cls == &lv_canvas_class)     return "canvas";
#endif
#if LV_USE_SPINBOX
    if (cls == &lv_spinbox_class)    return "spinbox";
#endif
#if LV_USE_WIN
    if (cls == &lv_win_class)        return "win";
#endif
    return "obj";
}

/* Get label text from an object (if it's a label, or its first child label) */
static const char* get_widget_text(lv_obj_t* obj) {
    if (lv_obj_get_class(obj) == &lv_label_class) {
        return lv_label_get_text(obj);
    }
    /* Check first child for button labels etc. */
    uint32_t cnt = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(obj, (int32_t)i);
        if (lv_obj_get_class(child) == &lv_label_class) {
            return lv_label_get_text(child);
        }
    }
    return NULL;
}

/* Get object name if available */
static const char* get_obj_name(lv_obj_t* obj) {
#if defined(LV_USE_OBJ_NAME) && LV_USE_OBJ_NAME
    return lv_obj_get_name(obj);
#else
    (void)obj;
    return NULL;
#endif
}

/* ======================== Auto-Path System ======================== */

/* Build auto-path like "button[Submit]" or "slider[1]" */
static void build_auto_path(lv_obj_t* obj, char* buf, int max_len) {
    const char* type = widget_type_name(obj);
    const char* text = get_widget_text(obj);
    const char* name = get_obj_name(obj);

    if (name && name[0]) {
        snprintf(buf, max_len, "%s", name);
        return;
    }

    if (text && text[0]) {
        snprintf(buf, max_len, "%s[%s]", type, text);
        return;
    }

    /* Index-based: count siblings of same type */
    lv_obj_t* parent = lv_obj_get_parent(obj);
    if (!parent) {
        snprintf(buf, max_len, "%s", type);
        return;
    }

    int idx = 0;
    const lv_obj_class_t* cls = lv_obj_get_class(obj);
    uint32_t cnt = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t* sibling = lv_obj_get_child(parent, (int32_t)i);
        if (sibling == obj) break;
        if (lv_obj_get_class(sibling) == cls) idx++;
    }
    snprintf(buf, max_len, "%s[%d]", type, idx);
}

/* ======================== Widget Tree Serialization ======================== */

static void serialize_widget(lv_obj_t* obj, int depth) {
    if (depth > LVV_MAX_TREE_DEPTH) return;

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    char auto_path[128];
    build_auto_path(obj, auto_path, sizeof(auto_path));

    const char* name = get_obj_name(obj);
    const char* text = get_widget_text(obj);
    const char* type = widget_type_name(obj);

    bool visible = lv_obj_is_visible(obj);
    bool clickable = lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    resp_append("{");
    resp_append("\"type\":"); resp_append_str(type);
    resp_append(",\"name\":"); resp_append_str(name ? name : "");
    resp_append(",\"auto_path\":"); resp_append_str(auto_path);
    resp_append(",\"text\":"); resp_append_str(text ? text : "");
    resp_append(",\"x\":"); resp_append_int(coords.x1);
    resp_append(",\"y\":"); resp_append_int(coords.y1);
    resp_append(",\"width\":"); resp_append_int(lv_obj_get_width(obj));
    resp_append(",\"height\":"); resp_append_int(lv_obj_get_height(obj));
    resp_append(",\"visible\":"); resp_append_bool(visible);
    resp_append(",\"clickable\":"); resp_append_bool(clickable);
    resp_append(",\"id\":"); resp_append_int((intptr_t)obj);

    uint32_t child_cnt = lv_obj_get_child_count(obj);
    if (child_cnt > 0) {
        resp_append(",\"children\":[");
        for (uint32_t i = 0; i < child_cnt; i++) {
            if (i > 0) resp_append(",");
            serialize_widget(lv_obj_get_child(obj, (int32_t)i), depth + 1);
        }
        resp_append("]");
    }

    resp_append("}");
}

/* ======================== Widget Finder ======================== */

static lv_obj_t* find_widget_recursive(lv_obj_t* obj, const char* selector) {
    const char* name = get_obj_name(obj);
    if (name && strcmp(name, selector) == 0) return obj;

    char auto_path[128];
    build_auto_path(obj, auto_path, sizeof(auto_path));
    if (strcmp(auto_path, selector) == 0) return obj;

    uint32_t cnt = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t* found = find_widget_recursive(
            lv_obj_get_child(obj, (int32_t)i), selector);
        if (found) return found;
    }
    return NULL;
}

static lv_obj_t* find_widget(const char* selector) {
    lv_obj_t* screen = lv_screen_active();
    if (!screen) return NULL;
    return find_widget_recursive(screen, selector);
}

/* ======================== Input Simulation ======================== */

/* Spy-owned virtual input device. LVGL's timer handler polls this via the
 * read callback, which produces proper press/move/release sequences through
 * LVGL's normal input pipeline (scrolling, gestures, long-press, etc.). */

static lv_indev_t* s_spy_indev = NULL;
static lv_indev_state_t s_indev_state = LV_INDEV_STATE_RELEASED;
static lv_point_t s_indev_point = {0, 0};

static void spy_indev_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    data->point = s_indev_point;
    data->state = s_indev_state;
    data->continue_reading = false;
}

static void ensure_spy_indev(void) {
    if (s_spy_indev) return;
    s_spy_indev = lv_indev_create();
    lv_indev_set_type(s_spy_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_spy_indev, spy_indev_read_cb);
    lv_indev_set_display(s_spy_indev, lv_display_get_default());
}

/* Force the spy indev timer to fire on the next lv_timer_handler() call,
 * regardless of its period. Without this, the main loop's lv_timer_handler()
 * already ran the indev timer moments ago, so our call would skip it. */
static void force_indev_process(void) {
    if (!s_spy_indev) return;
    lv_timer_t* t = lv_indev_get_read_timer(s_spy_indev);
    if (t) lv_timer_ready(t);
    lv_timer_handler();
}

/* Press at point and force LVGL to process it */
static void inject_press(int x, int y) {
    ensure_spy_indev();
    s_indev_point.x = (int32_t)x;
    s_indev_point.y = (int32_t)y;
    s_indev_state = LV_INDEV_STATE_PRESSED;
    force_indev_process();
}

/* Move to point while pressed */
static void inject_move(int x, int y) {
    ensure_spy_indev();
    s_indev_point.x = (int32_t)x;
    s_indev_point.y = (int32_t)y;
    force_indev_process();
}

/* Release and force LVGL to process */
static void inject_release(void) {
    ensure_spy_indev();
    s_indev_state = LV_INDEV_STATE_RELEASED;
    force_indev_process();
}

/* Full click: press, then release after forcing the indev timer each time */
static void inject_click(int x, int y) {
    inject_press(x, y);
    inject_release();
}

static void inject_key_text(const char* text) {
    lv_obj_t* focused = NULL;
    lv_group_t* group = lv_group_get_default();
    if (group) {
        focused = lv_group_get_focused(group);
    }
    if (!focused) return;

    for (const char* p = text; *p; p++) {
        uint32_t c = (uint32_t)(unsigned char)*p;
        lv_obj_send_event(focused, LV_EVENT_KEY, &c);
    }

#if LV_USE_TEXTAREA
    /* For textareas, use the direct API */
    if (lv_obj_get_class(focused) == &lv_textarea_class) {
        lv_textarea_add_text(focused, text);
    }
#endif
}

/* Forward declaration for binary screenshot */
static void send_response(void);

/* ======================== Screenshot ======================== */

#if LV_USE_SNAPSHOT

/* Binary screenshot: JSON header line + raw pixel bytes (no base64).
 * The caller must NOT call send_response() after this — it handles its own I/O. */
static void cmd_screenshot(void) {
    lv_obj_t* screen = lv_screen_active();
    if (!screen) {
        resp_reset();
        resp_append("{\"error\":\"No active screen\"}");
        return;
    }

    lv_draw_buf_t* buf = lv_snapshot_take(screen, LV_COLOR_FORMAT_ARGB8888);
    if (!buf) {
        resp_reset();
        resp_append("{\"error\":\"Snapshot failed\"}");
        return;
    }

    int w = buf->header.w;
    int h = buf->header.h;
    int cf = (int)buf->header.cf;
    uint8_t* pixels = buf->data;
    int stride = buf->header.stride;
    int data_size = stride * h;

    /* Send JSON header line with data_size */
    resp_reset();
    resp_append("{\"format\":");
    resp_append_int(cf);
    resp_append(",\"width\":");
    resp_append_int(w);
    resp_append(",\"height\":");
    resp_append_int(h);
    resp_append(",\"stride\":");
    resp_append_int(stride);
    resp_append(",\"data_size\":");
    resp_append_int(data_size);
    resp_append("}");
    send_response();

    /* Send raw pixel bytes directly */
    if (s_client_fd >= 0) {
        int total = 0;
        while (total < data_size) {
            int n = (int)send(s_client_fd, pixels + total,
                              data_size - total, MSG_NOSIGNAL);
            if (n <= 0) {
                close(s_client_fd);
                s_client_fd = -1;
                break;
            }
            total += n;
        }
    }

    lv_draw_buf_destroy(buf);
    /* Clear resp so send_response() in caller is a no-op */
    resp_reset();
}

#else /* !LV_USE_SNAPSHOT */

static void cmd_screenshot(void) {
    resp_reset();
    resp_append("{\"error\":\"Snapshot not enabled. Set LV_USE_SNAPSHOT=1 in lv_conf.h\"}");
}

#endif /* LV_USE_SNAPSHOT */

/* ======================== Command Processing ======================== */

static void process_command(const char* cmd) {
    char cmd_name[64] = {0};
    json_get_string(cmd, "cmd", cmd_name, sizeof(cmd_name));

    resp_reset();

    if (strcmp(cmd_name, "ping") == 0) {
        resp_append("{\"version\":\"" LVV_VERSION "\"}");
    }
    else if (strcmp(cmd_name, "get_tree") == 0) {
        lv_obj_t* screen = lv_screen_active();
        if (!screen) {
            resp_append("{\"error\":\"No active screen\"}");
            return;
        }
        resp_append("{\"tree\":");
        serialize_widget(screen, 0);
        resp_append("}");
    }
    else if (strcmp(cmd_name, "find") == 0) {
        char name[128] = {0};
        json_get_string(cmd, "name", name, sizeof(name));

        lv_obj_t* obj = find_widget(name);
        if (obj) {
            resp_append("{\"widget\":");
            serialize_widget(obj, 0);
            resp_append("}");
        } else {
            resp_append("{\"error\":\"Widget not found\"}");
        }
    }
    else if (strcmp(cmd_name, "click") == 0) {
        char name[128] = {0};
        json_get_string(cmd, "name", name, sizeof(name));

        lv_obj_t* obj = find_widget(name);
        if (obj) {
            lv_area_t coords;
            lv_obj_get_coords(obj, &coords);
            int cx = (coords.x1 + coords.x2) / 2;
            int cy = (coords.y1 + coords.y2) / 2;
            inject_click(cx, cy);
            resp_append("{\"success\":true}");
        } else {
            resp_append("{\"error\":\"Widget not found\"}");
        }
    }
    else if (strcmp(cmd_name, "click_at") == 0) {
        int x = 0, y = 0;
        json_get_int(cmd, "x", &x);
        json_get_int(cmd, "y", &y);
        inject_click(x, y);
        resp_append("{\"success\":true}");
    }
    else if (strcmp(cmd_name, "type") == 0) {
        char text[512] = {0};
        json_get_string(cmd, "text", text, sizeof(text));
        inject_key_text(text);
        resp_append("{\"success\":true}");
    }
    else if (strcmp(cmd_name, "key") == 0) {
        char key[32] = {0};
        json_get_string(cmd, "key", key, sizeof(key));

        uint32_t lv_key = 0;
        if (strcmp(key, "UP") == 0)         lv_key = LV_KEY_UP;
        else if (strcmp(key, "DOWN") == 0)  lv_key = LV_KEY_DOWN;
        else if (strcmp(key, "LEFT") == 0)  lv_key = LV_KEY_LEFT;
        else if (strcmp(key, "RIGHT") == 0) lv_key = LV_KEY_RIGHT;
        else if (strcmp(key, "ENTER") == 0) lv_key = LV_KEY_ENTER;
        else if (strcmp(key, "ESC") == 0)   lv_key = LV_KEY_ESC;
        else if (strcmp(key, "BACKSPACE") == 0) lv_key = LV_KEY_BACKSPACE;
        else if (strcmp(key, "NEXT") == 0)  lv_key = LV_KEY_NEXT;
        else if (strcmp(key, "PREV") == 0)  lv_key = LV_KEY_PREV;

        if (lv_key) {
            lv_group_t* group = lv_group_get_default();
            if (group) {
                lv_obj_t* focused = lv_group_get_focused(group);
                if (focused) {
                    lv_obj_send_event(focused, LV_EVENT_KEY, &lv_key);
                }
            }
            resp_append("{\"success\":true}");
        } else {
            resp_append("{\"error\":\"Unknown key\"}");
        }
    }
    else if (strcmp(cmd_name, "screenshot") == 0) {
        cmd_screenshot();
        return;  /* response already sent (binary transfer) */
    }
    else if (strcmp(cmd_name, "get_screen_info") == 0) {
        lv_display_t* disp = lv_display_get_default();
        if (disp) {
            resp_append("{\"width\":");
            resp_append_int(lv_display_get_horizontal_resolution(disp));
            resp_append(",\"height\":");
            resp_append_int(lv_display_get_vertical_resolution(disp));
            resp_append(",\"color_format\":\"ARGB8888\"}");
        } else {
            resp_append("{\"error\":\"No display\"}");
        }
    }
    else if (strcmp(cmd_name, "get_props") == 0) {
        char name[128] = {0};
        json_get_string(cmd, "name", name, sizeof(name));

        lv_obj_t* obj = find_widget(name);
        if (obj) {
            lv_area_t coords;
            lv_obj_get_coords(obj, &coords);
            const char* text = get_widget_text(obj);

            resp_append("{");
            resp_append("\"type\":"); resp_append_str(widget_type_name(obj));
            resp_append(",\"x\":"); resp_append_int(coords.x1);
            resp_append(",\"y\":"); resp_append_int(coords.y1);
            resp_append(",\"width\":"); resp_append_int(lv_obj_get_width(obj));
            resp_append(",\"height\":"); resp_append_int(lv_obj_get_height(obj));
            resp_append(",\"visible\":"); resp_append_bool(lv_obj_is_visible(obj));
            resp_append(",\"clickable\":"); resp_append_bool(lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE));
            if (text) { resp_append(",\"text\":"); resp_append_str(text); }
            resp_append("}");
        } else {
            resp_append("{\"error\":\"Widget not found\"}");
        }
    }
    else if (strcmp(cmd_name, "press") == 0) {
        int x = 0, y = 0;
        json_get_int(cmd, "x", &x);
        json_get_int(cmd, "y", &y);
        inject_press(x, y);
        resp_append("{\"success\":true}");
    }
    else if (strcmp(cmd_name, "release") == 0) {
        inject_release();
        resp_append("{\"success\":true}");
    }
    else if (strcmp(cmd_name, "move_to") == 0) {
        int x = 0, y = 0;
        json_get_int(cmd, "x", &x);
        json_get_int(cmd, "y", &y);
        inject_move(x, y);
        resp_append("{\"success\":true}");
    }
    else if (strcmp(cmd_name, "swipe") == 0) {
        int x = 0, y = 0, x_end = 0, y_end = 0, duration = 300;
        json_get_int(cmd, "x", &x);
        json_get_int(cmd, "y", &y);
        json_get_int(cmd, "x_end", &x_end);
        json_get_int(cmd, "y_end", &y_end);
        json_get_int(cmd, "duration", &duration);

        /* Interpolate press -> move -> release over duration */
        int steps = duration / 16;  /* ~60fps steps */
        if (steps < 2) steps = 2;

        inject_press(x, y);
        for (int i = 1; i <= steps; i++) {
            int cx = x + (x_end - x) * i / steps;
            int cy = y + (y_end - y) * i / steps;
            inject_move(cx, cy);
            lv_delay_ms(16);
        }
        inject_release();
        resp_append("{\"success\":true}");
    }
    else {
        resp_append("{\"error\":\"Unknown command: ");
        resp_append(cmd_name);
        resp_append("\"}");
    }
}

/* ======================== Network ======================== */

static void send_response(void) {
    if (s_client_fd < 0 || resp_len == 0) return;

    /* Append newline delimiter */
    resp_append("\n");

    int total = 0;
    while (total < resp_len) {
        int n = (int)send(s_client_fd, s_resp_buf + total, resp_len - total, MSG_NOSIGNAL);
        if (n <= 0) {
            close(s_client_fd);
            s_client_fd = -1;
            return;
        }
        total += n;
    }
}

static void accept_client(void) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int fd = accept(s_listen_fd, (struct sockaddr*)&addr, &len);
    if (fd < 0) return;

    /* Only one client at a time */
    if (s_client_fd >= 0) {
        close(s_client_fd);
    }
    s_client_fd = fd;
    set_nonblocking(s_client_fd);
    s_cmd_len = 0;

    printf("[lvv_spy] Client connected from %s:%d\n",
           inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
}

static void read_client(void) {
    if (s_client_fd < 0) return;

    char buf[1024];
    int n = (int)recv(s_client_fd, buf, sizeof(buf), 0);
    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            printf("[lvv_spy] Client disconnected\n");
            close(s_client_fd);
            s_client_fd = -1;
            s_cmd_len = 0;
        }
        return;
    }

    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            s_cmd_buf[s_cmd_len] = '\0';
            if (s_cmd_len > 0) {
                process_command(s_cmd_buf);
                send_response();
            }
            s_cmd_len = 0;
        } else if (s_cmd_len < LVV_MAX_CMD_LEN - 1) {
            s_cmd_buf[s_cmd_len++] = buf[i];
        }
    }
}

/* ======================== Public API ======================== */

bool lvv_spy_init(uint16_t port) {
    s_resp_buf = (char*)malloc(LVV_MAX_RESP_LEN);
    if (!s_resp_buf) {
        fprintf(stderr, "[lvv_spy] Failed to allocate response buffer\n");
        return false;
    }

    s_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen_fd < 0) {
        fprintf(stderr, "[lvv_spy] socket() failed: %s\n", strerror(errno));
        return false;
    }

    int opt = 1;
    setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(s_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[lvv_spy] bind() failed on port %d: %s\n",
                port, strerror(errno));
        close(s_listen_fd);
        s_listen_fd = -1;
        return false;
    }

    if (listen(s_listen_fd, 1) < 0) {
        fprintf(stderr, "[lvv_spy] listen() failed: %s\n", strerror(errno));
        close(s_listen_fd);
        s_listen_fd = -1;
        return false;
    }

    set_nonblocking(s_listen_fd);

    printf("[lvv_spy] Listening on port %d (v%s)\n", port, LVV_VERSION);
    return true;
}

void lvv_spy_process(void) {
    if (s_listen_fd < 0) return;

    /* Check for new connections */
    struct pollfd pfds[2];
    int nfds = 0;

    pfds[nfds].fd = s_listen_fd;
    pfds[nfds].events = POLLIN;
    nfds++;

    if (s_client_fd >= 0) {
        pfds[nfds].fd = s_client_fd;
        pfds[nfds].events = POLLIN;
        nfds++;
    }

    int ret = poll(pfds, nfds, 0);  /* Non-blocking */
    if (ret <= 0) return;

    if (pfds[0].revents & POLLIN) {
        accept_client();
    }

    if (nfds > 1 && (pfds[1].revents & POLLIN)) {
        read_client();
    }
}

void lvv_spy_deinit(void) {
    if (s_client_fd >= 0) {
        close(s_client_fd);
        s_client_fd = -1;
    }
    if (s_listen_fd >= 0) {
        close(s_listen_fd);
        s_listen_fd = -1;
    }
    if (s_resp_buf) {
        free(s_resp_buf);
        s_resp_buf = NULL;
    }
    s_cmd_len = 0;
}

bool lvv_spy_is_connected(void) {
    return s_client_fd >= 0;
}
