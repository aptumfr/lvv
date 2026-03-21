/**
 * @file lvv_spy.c
 * @brief LVV Spy - Embeddable LVGL introspection server
 *
 * Single-file implementation. Listens on a TCP port, accepts one client,
 * processes newline-delimited JSON commands, returns JSON responses.
 *
 * Portable: uses only LVGL APIs for memory/string operations.
 * The only libc dependency is <string.h> for strstr() and strtol().
 * Networking is behind #ifdef LVV_USE_POSIX_SOCKETS (default on Linux/macOS/ESP-IDF).
 */

#include "lvv_spy.h"

#include <lvgl.h>
#include <string.h>  /* strstr, strtol — universally available, even on bare-metal */

/* LVGL private headers for strip-based rendering and log globals */
#include "src/draw/lv_draw_private.h"
#include "src/core/lv_obj_draw_private.h"
#include "src/core/lv_refr_private.h"
#include "src/display/lv_display_private.h"
#include "src/core/lv_global.h"

/* Transport: POSIX sockets on Linux/macOS/ESP-IDF.
 * Auto-detected from platform defines. Define LVV_NO_POSIX_SOCKETS to force off,
 * or LVV_USE_POSIX_SOCKETS=1 to force on for custom POSIX-compatible targets. */
#ifndef LVV_USE_POSIX_SOCKETS
#if defined(__linux__) || defined(__APPLE__) || defined(ESP_PLATFORM) || defined(__unix__)
#define LVV_USE_POSIX_SOCKETS 1
#else
#define LVV_USE_POSIX_SOCKETS 0
#endif
#endif

#ifdef LVV_NO_POSIX_SOCKETS
#undef LVV_USE_POSIX_SOCKETS
#define LVV_USE_POSIX_SOCKETS 0
#endif

#if LVV_USE_POSIX_SOCKETS
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#endif

/* ======================== Configuration ======================== */

#define LVV_VERSION       "0.1.0"
#define LVV_MAX_CMD_LEN   4096
#define LVV_MAX_RESP_LEN  (1 * 1024 * 1024)  /* 1MB for tree responses */
#define LVV_MAX_TREE_DEPTH 32
#define LVV_STRIP_HEIGHT   60  /* rows per screenshot strip */
#define LVV_LOG_RING_SIZE  64  /* max captured log entries */
#define LVV_LOG_MAX_LEN    256 /* max chars per log entry */

/* ======================== State ======================== */

static int s_listen_fd = -1;
static int s_client_fd = -1;
static char s_cmd_buf[LVV_MAX_CMD_LEN];
static int s_cmd_len = 0;

/* --- Log capture ring buffer --- */
static char s_log_ring[LVV_LOG_RING_SIZE][LVV_LOG_MAX_LEN];
static int  s_log_head = 0;   /* next write position */
static int  s_log_count = 0;  /* entries in buffer */
static bool s_log_capturing = false;
static lv_log_print_g_cb_t s_prev_log_cb = NULL;

/* --- Performance metrics --- */
static uint32_t s_poll_count = 0;      /* calls to lvv_spy_process() in current window */
static uint32_t s_poll_timestamp = 0;  /* start of current 1s window */
static uint32_t s_poll_rate = 0;       /* polls per second (last complete window) */
static char* s_resp_buf = NULL;

/* ======================== Helpers ======================== */

#if LVV_USE_POSIX_SOCKETS
static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
#endif

/* Simple JSON string append helpers (no external JSON library needed) */

static int resp_len;

static void resp_reset(void) {
    resp_len = 0;
    s_resp_buf[0] = '\0';
}

static void resp_append(const char* s) {
    int len = (int)lv_strlen(s);
    if (resp_len + len < LVV_MAX_RESP_LEN) {
        lv_memcpy(s_resp_buf + resp_len, s, len);
        resp_len += len;
        s_resp_buf[resp_len] = '\0';
    }
}

static void resp_append_int(int64_t val) {
    char buf[32];
    lv_snprintf(buf, sizeof(buf), "%ld", (long)val);
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
    lv_snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) return false;

    pos += lv_strlen(search);
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
    lv_snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) return false;

    pos += lv_strlen(search);
    while (*pos == ' ' || *pos == ':') pos++;

    char* end;
    long val = strtol(pos, &end, 10);
    if (end == pos) return false;
    *out = (int)val;
    return true;
}

/* ======================== Log Capture ======================== */

static void lvv_log_cb(lv_log_level_t level, const char* buf) {
    /* Forward to previous callback so app logging isn't lost */
    if (s_prev_log_cb) s_prev_log_cb(level, buf);
    if (!s_log_capturing) return;
    /* Store in ring buffer */
    lv_strncpy(s_log_ring[s_log_head], buf, LVV_LOG_MAX_LEN);
    /* Strip trailing newline */
    int len = (int)lv_strlen(s_log_ring[s_log_head]);
    if (len > 0 && s_log_ring[s_log_head][len - 1] == '\n') {
        s_log_ring[s_log_head][len - 1] = '\0';
    }
    s_log_head = (s_log_head + 1) % LVV_LOG_RING_SIZE;
    if (s_log_count < LVV_LOG_RING_SIZE) s_log_count++;
}

/* ======================== Performance Metrics ======================== */

/* Called from lvv_spy_process() to track poll rate */
static void lvv_update_metrics(void) {
    uint32_t now = lv_tick_get();
    s_poll_count++;
    if (s_poll_timestamp == 0) s_poll_timestamp = now;
    uint32_t elapsed = now - s_poll_timestamp;
    if (elapsed >= 1000) {
        s_poll_rate = (uint32_t)((uint64_t)s_poll_count * 1000 / elapsed);
        s_poll_count = 0;
        s_poll_timestamp = now;
    }
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
        lv_snprintf(buf, max_len, "%s", name);
        return;
    }

    if (text && text[0]) {
        lv_snprintf(buf, max_len, "%s[%s]", type, text);
        return;
    }

    /* Index-based: count siblings of same type */
    lv_obj_t* parent = lv_obj_get_parent(obj);
    if (!parent) {
        lv_snprintf(buf, max_len, "%s", type);
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
    lv_snprintf(buf, max_len, "%s[%d]", type, idx);
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
    if (name && lv_strcmp(name, selector) == 0) return obj;

    char auto_path[128];
    build_auto_path(obj, auto_path, sizeof(auto_path));
    if (lv_strcmp(auto_path, selector) == 0) return obj;

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
#define LVV_INPUT_QUEUE_SIZE 64

static lv_indev_t* s_spy_indev = NULL;
static lv_indev_state_t s_indev_state = LV_INDEV_STATE_RELEASED;
static lv_point_t s_indev_point = {0, 0};
static lv_indev_state_t s_pending_indev_state = LV_INDEV_STATE_RELEASED;
static lv_point_t s_pending_indev_point = {0, 0};

typedef struct {
    lv_indev_state_t state;
    lv_point_t point;
} lvv_input_sample_t;

static lvv_input_sample_t s_input_queue[LVV_INPUT_QUEUE_SIZE];
static uint16_t s_input_queue_head = 0;
static uint16_t s_input_queue_tail = 0;
static uint16_t s_input_queue_count = 0;

static void spy_indev_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;

    if (s_input_queue_count > 0) {
        lvv_input_sample_t sample = s_input_queue[s_input_queue_head];
        s_input_queue_head = (uint16_t)((s_input_queue_head + 1) % LVV_INPUT_QUEUE_SIZE);
        s_input_queue_count--;
        s_indev_point = sample.point;
        s_indev_state = sample.state;
        data->continue_reading = (s_input_queue_count > 0);
    } else {
        data->continue_reading = false;
    }

    data->point = s_indev_point;
    data->state = s_indev_state;
}

static void ensure_spy_indev(void) {
    if (s_spy_indev) return;
    s_spy_indev = lv_indev_create();
    lv_indev_set_type(s_spy_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_spy_indev, spy_indev_read_cb);
    lv_indev_set_display(s_spy_indev, lv_display_get_default());
}

static bool queue_input_sample(lv_indev_state_t state, int x, int y) {
    if (s_input_queue_count >= LVV_INPUT_QUEUE_SIZE) return false;

    s_input_queue[s_input_queue_tail].state = state;
    s_input_queue[s_input_queue_tail].point.x = (int32_t)x;
    s_input_queue[s_input_queue_tail].point.y = (int32_t)y;
    s_input_queue_tail = (uint16_t)((s_input_queue_tail + 1) % LVV_INPUT_QUEUE_SIZE);
    s_input_queue_count++;
    s_pending_indev_state = state;
    s_pending_indev_point.x = (int32_t)x;
    s_pending_indev_point.y = (int32_t)y;
    return true;
}

static void poke_spy_indev(void) {
    if (!s_spy_indev) return;
    lv_timer_t* t = lv_indev_get_read_timer(s_spy_indev);
    if (t) lv_timer_ready(t);
}

/* Queue a press for LVGL's normal input pipeline instead of running
 * lv_timer_handler() recursively from the spy context. */
static void inject_press(int x, int y) {
    ensure_spy_indev();
    if (queue_input_sample(LV_INDEV_STATE_PRESSED, x, y)) {
        poke_spy_indev();
    }
}

/* Queue move samples using the pending pointer state so drag sequences stay
 * pressed even if the previous sample has not been consumed yet. */
static void inject_move(int x, int y) {
    ensure_spy_indev();
    if (queue_input_sample(s_pending_indev_state, x, y)) {
        poke_spy_indev();
    }
}

static void inject_release(void) {
    ensure_spy_indev();
    if (queue_input_sample(LV_INDEV_STATE_RELEASED,
                           s_pending_indev_point.x,
                           s_pending_indev_point.y)) {
        poke_spy_indev();
    }
}

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

    /* For textareas, use the direct API */
#if LV_USE_TEXTAREA
    if (lv_obj_get_class(focused) == &lv_textarea_class) {
        lv_textarea_add_text(focused, text);
    }
#endif
}

/* Forward declaration for binary screenshot */
static void send_response(void);

/* ======================== Screenshot (strip-based) ======================== */

/* Send raw bytes over TCP. Returns false on error. */
#if LVV_USE_POSIX_SOCKETS
static bool send_raw(const uint8_t* data, int size) {
    if (s_client_fd < 0) return false;
    int total = 0;
    while (total < size) {
        int n = (int)send(s_client_fd, data + total, size - total, MSG_NOSIGNAL);
        if (n <= 0) {
            close(s_client_fd);
            s_client_fd = -1;
            return false;
        }
        total += n;
    }
    return true;
}
#endif

/* Render one horizontal strip of the screen into draw_buf and send it.
 * The layer/display context must already be set up by the caller. */
static bool render_and_send_strip(lv_obj_t* screen, lv_draw_buf_t* draw_buf,
                                  lv_display_t* disp, int32_t scr_w,
                                  int32_t y0, int32_t y1, lv_color_format_t cf) {
    int32_t rows = y1 - y0 + 1;
    int stride = (int)draw_buf->header.stride;

    lv_draw_buf_clear(draw_buf, NULL);

    lv_layer_t layer;
    lv_layer_init(&layer);

    layer.draw_buf = draw_buf;
    layer.buf_area.x1 = 0;
    layer.buf_area.y1 = y0;
    layer.buf_area.x2 = scr_w - 1;
    layer.buf_area.y2 = y0 + (int32_t)draw_buf->header.h - 1;
    layer.color_format = cf;
    layer._clip_area.x1 = 0;
    layer._clip_area.y1 = y0;
    layer._clip_area.x2 = scr_w - 1;
    layer._clip_area.y2 = y1;
    layer.phy_clip_area = layer._clip_area;

    lv_draw_unit_send_event(NULL, LV_EVENT_CHILD_CREATED, &layer);

    lv_layer_t* layer_old = disp->layer_head;
    disp->layer_head = &layer;

    lv_obj_redraw(&layer, screen);

    layer.all_tasks_added = true;
    while (layer.draw_task_head) {
        lv_draw_dispatch_wait_for_request();
        lv_draw_dispatch();
    }

    disp->layer_head = layer_old;

    lv_draw_unit_send_event(NULL, LV_EVENT_SCREEN_LOAD_START, &layer);
    lv_draw_unit_send_event(NULL, LV_EVENT_CHILD_DELETED, &layer);

    return send_raw(draw_buf->data, stride * rows);
}

/* Strip-based screenshot: renders the screen in horizontal strips,
 * sending each strip's pixels immediately over TCP. Peak memory is
 * strip_height * stride instead of full_height * stride. */
static void cmd_screenshot(void) {
    lv_obj_t* screen = lv_screen_active();
    if (!screen) {
        resp_reset();
        resp_append("{\"error\":\"No active screen\"}");
        return;
    }

    lv_obj_update_layout(screen);

    lv_display_t* disp = lv_display_get_default();
    if (!disp) {
        resp_reset();
        resp_append("{\"error\":\"No display\"}");
        return;
    }

    int32_t scr_w = lv_display_get_horizontal_resolution(disp);
    int32_t scr_h = lv_display_get_vertical_resolution(disp);

    /* Use the display's native format when the host decoder supports it,
     * otherwise fall back to ARGB8888. This avoids a pixel conversion
     * inside LVGL and halves transfer size on RGB565 boards. */
    lv_color_format_t cf = lv_display_get_color_format(disp);
    switch (cf) {
        case LV_COLOR_FORMAT_RGB565:
        case LV_COLOR_FORMAT_RGB888:
        case LV_COLOR_FORMAT_XRGB8888:
        case LV_COLOR_FORMAT_ARGB8888:
            break;  /* supported by host decoder */
        default:
            cf = LV_COLOR_FORMAT_ARGB8888;  /* safe fallback */
            break;
    }

    int strip_h = LVV_STRIP_HEIGHT;

    /* Create strip buffer (reused across strips) */
    lv_draw_buf_t* strip_buf = lv_draw_buf_create(scr_w, strip_h, cf, LV_STRIDE_AUTO);
    if (!strip_buf) {
        resp_reset();
        resp_append("{\"error\":\"Failed to allocate strip buffer\"}");
        return;
    }

    int stride = (int)strip_buf->header.stride;
    int data_size = stride * (int)scr_h;

    /* Send JSON header — same format as before, host sees no difference */
    resp_reset();
    resp_append("{\"format\":");
    resp_append_int((int)cf);
    resp_append(",\"width\":");
    resp_append_int(scr_w);
    resp_append(",\"height\":");
    resp_append_int(scr_h);
    resp_append(",\"stride\":");
    resp_append_int(stride);
    resp_append(",\"data_size\":");
    resp_append_int(data_size);
    resp_append("}");
    send_response();

    /* Set up display refresh context (once, outside the loop) */
    lv_display_t* disp_old = lv_refr_get_disp_refreshing();
    lv_refr_set_disp_refreshing(disp);

    /* Render and send each strip */
    for (int32_t y = 0; y < scr_h; y += strip_h) {
        int32_t y_end = y + strip_h - 1;
        if (y_end >= scr_h) y_end = scr_h - 1;

        if (!render_and_send_strip(screen, strip_buf, disp, scr_w, y, y_end, cf)) {
            break;  /* client disconnected */
        }
    }

    lv_refr_set_disp_refreshing(disp_old);
    lv_draw_buf_destroy(strip_buf);

    /* Clear resp so send_response() in caller is a no-op */
    resp_reset();
}

/* ======================== Command Processing ======================== */

static void process_command(const char* cmd) {
    char cmd_name[64] = {0};
    json_get_string(cmd, "cmd", cmd_name, sizeof(cmd_name));

    resp_reset();

    if (lv_strcmp(cmd_name, "ping") == 0) {
        resp_append("{\"version\":\"" LVV_VERSION "\"}");
    }
    else if (lv_strcmp(cmd_name, "get_tree") == 0) {
        lv_obj_t* screen = lv_screen_active();
        if (!screen) {
            resp_append("{\"error\":\"No active screen\"}");
            return;
        }
        resp_append("{\"tree\":");
        serialize_widget(screen, 0);
        resp_append("}");
    }
    else if (lv_strcmp(cmd_name, "find") == 0) {
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
    else if (lv_strcmp(cmd_name, "click") == 0) {
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
    else if (lv_strcmp(cmd_name, "click_at") == 0) {
        int x = 0, y = 0;
        json_get_int(cmd, "x", &x);
        json_get_int(cmd, "y", &y);
        inject_click(x, y);
        resp_append("{\"success\":true}");
    }
    else if (lv_strcmp(cmd_name, "type") == 0) {
        char text[512] = {0};
        json_get_string(cmd, "text", text, sizeof(text));
        inject_key_text(text);
        resp_append("{\"success\":true}");
    }
    else if (lv_strcmp(cmd_name, "key") == 0) {
        char key[32] = {0};
        json_get_string(cmd, "key", key, sizeof(key));

        uint32_t lv_key = 0;
        if (lv_strcmp(key, "UP") == 0)         lv_key = LV_KEY_UP;
        else if (lv_strcmp(key, "DOWN") == 0)  lv_key = LV_KEY_DOWN;
        else if (lv_strcmp(key, "LEFT") == 0)  lv_key = LV_KEY_LEFT;
        else if (lv_strcmp(key, "RIGHT") == 0) lv_key = LV_KEY_RIGHT;
        else if (lv_strcmp(key, "ENTER") == 0) lv_key = LV_KEY_ENTER;
        else if (lv_strcmp(key, "ESC") == 0)   lv_key = LV_KEY_ESC;
        else if (lv_strcmp(key, "BACKSPACE") == 0) lv_key = LV_KEY_BACKSPACE;
        else if (lv_strcmp(key, "NEXT") == 0)  lv_key = LV_KEY_NEXT;
        else if (lv_strcmp(key, "PREV") == 0)  lv_key = LV_KEY_PREV;

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
    else if (lv_strcmp(cmd_name, "screenshot") == 0) {
        cmd_screenshot();
        return;  /* response already sent (binary transfer) */

    }
    else if (lv_strcmp(cmd_name, "get_screen_info") == 0) {
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
    else if (lv_strcmp(cmd_name, "get_props") == 0) {
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
    else if (lv_strcmp(cmd_name, "press") == 0) {
        int x = 0, y = 0;
        json_get_int(cmd, "x", &x);
        json_get_int(cmd, "y", &y);
        inject_press(x, y);
        resp_append("{\"success\":true}");
    }
    else if (lv_strcmp(cmd_name, "release") == 0) {
        inject_release();
        resp_append("{\"success\":true}");
    }
    else if (lv_strcmp(cmd_name, "move_to") == 0) {
        int x = 0, y = 0;
        json_get_int(cmd, "x", &x);
        json_get_int(cmd, "y", &y);
        inject_move(x, y);
        resp_append("{\"success\":true}");
    }
    else if (lv_strcmp(cmd_name, "swipe") == 0) {
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
    else if (lv_strcmp(cmd_name, "get_logs") == 0) {
        resp_append("{\"logs\":[");
        if (s_log_count > 0) {
            int start = (s_log_count < LVV_LOG_RING_SIZE)
                ? 0
                : s_log_head;  /* oldest entry when ring is full */
            for (int i = 0; i < s_log_count; i++) {
                int idx = (start + i) % LVV_LOG_RING_SIZE;
                if (i > 0) resp_append(",");
                resp_append_str(s_log_ring[idx]);
            }
        }
        resp_append("]}");
    }
    else if (lv_strcmp(cmd_name, "clear_logs") == 0) {
        s_log_count = 0;
        s_log_head = 0;
        resp_append("{\"success\":true}");
    }
    else if (lv_strcmp(cmd_name, "set_log_capture") == 0) {
        int enable = 0;
        json_get_int(cmd, "enable", &enable);
        if (enable && !s_log_capturing) {
            lv_log_register_print_cb(lvv_log_cb);
            s_log_capturing = true;
        } else if (!enable && s_log_capturing) {
            /* Restore app's original callback (kept for future re-enable) */
            lv_log_register_print_cb(s_prev_log_cb);
            s_log_capturing = false;
        }
        resp_append("{\"success\":true,\"capturing\":");
        resp_append_bool(s_log_capturing);
        resp_append("}");
    }
    else if (lv_strcmp(cmd_name, "get_metrics") == 0) {
        resp_append("{\"poll_rate\":");
        resp_append_int(s_poll_rate);
        resp_append(",\"uptime_ms\":");
        resp_append_int(lv_tick_get());
        resp_append("}");
    }
    else {
        resp_append("{\"error\":\"Unknown command: ");
        resp_append(cmd_name);
        resp_append("\"}");
    }
}

/* ======================== Network (POSIX sockets) ======================== */

#if LVV_USE_POSIX_SOCKETS

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

    LV_LOG_INFO("lvv_spy: client connected");
}

static void read_client(void) {
    if (s_client_fd < 0) return;

    char buf[1024];
    int n = (int)recv(s_client_fd, buf, sizeof(buf), 0);
    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            LV_LOG_INFO("lvv_spy: client disconnected");
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
    /* Save the app's existing log callback so we can restore it later */
    s_prev_log_cb = LV_GLOBAL_DEFAULT()->custom_log_print_cb;

    s_input_queue_head = 0;
    s_input_queue_tail = 0;
    s_input_queue_count = 0;
    s_indev_state = LV_INDEV_STATE_RELEASED;
    s_indev_point.x = 0;
    s_indev_point.y = 0;
    s_pending_indev_state = LV_INDEV_STATE_RELEASED;
    s_pending_indev_point.x = 0;
    s_pending_indev_point.y = 0;

    s_resp_buf = (char*)lv_malloc(LVV_MAX_RESP_LEN);
    if (!s_resp_buf) {
        LV_LOG_ERROR("lvv_spy: failed to allocate response buffer");
        return false;
    }

    s_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen_fd < 0) {
        LV_LOG_ERROR("lvv_spy: socket() failed");
        return false;
    }

    int opt = 1;
    setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    lv_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(s_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LV_LOG_ERROR("lvv_spy: bind() failed on port %d", port);
        close(s_listen_fd);
        s_listen_fd = -1;
        return false;
    }

    if (listen(s_listen_fd, 1) < 0) {
        LV_LOG_ERROR("lvv_spy: listen() failed");
        close(s_listen_fd);
        s_listen_fd = -1;
        return false;
    }

    set_nonblocking(s_listen_fd);

    LV_LOG_INFO("lvv_spy: listening on port %d (v%s)", port, LVV_VERSION);
    return true;
}

void lvv_spy_process(void) {
    if (s_listen_fd < 0) return;
    lvv_update_metrics();

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
    /* Restore app's log callback if we took it over */
    if (s_log_capturing) {
        lv_log_register_print_cb(s_prev_log_cb);
        s_log_capturing = false;
    }
    if (s_client_fd >= 0) {
        close(s_client_fd);
        s_client_fd = -1;
    }
    if (s_listen_fd >= 0) {
        close(s_listen_fd);
        s_listen_fd = -1;
    }
    if (s_resp_buf) {
        lv_free(s_resp_buf);
        s_resp_buf = NULL;
    }
    s_cmd_len = 0;
}

bool lvv_spy_is_connected(void) {
    return s_client_fd >= 0;
}

#else /* !LVV_USE_POSIX_SOCKETS */

/* Stub implementations when no transport is available.
 * Users must implement a custom transport and call process_command() directly,
 * or define their own lvv_spy_init/process/deinit. */

static void send_response(void) { /* no-op */ }

bool lvv_spy_init(uint16_t port) {
    (void)port;
    s_prev_log_cb = LV_GLOBAL_DEFAULT()->custom_log_print_cb;
    s_resp_buf = (char*)lv_malloc(LVV_MAX_RESP_LEN);
    if (!s_resp_buf) {
        LV_LOG_ERROR("lvv_spy: failed to allocate response buffer");
        return false;
    }
    LV_LOG_ERROR("lvv_spy: no transport configured (define LVV_USE_POSIX_SOCKETS or implement custom transport)");
    lv_free(s_resp_buf);
    s_resp_buf = NULL;
    return false;
}

void lvv_spy_process(void) { lvv_update_metrics(); }
void lvv_spy_deinit(void) {
    if (s_log_capturing) {
        lv_log_register_print_cb(s_prev_log_cb);
        s_log_capturing = false;
    }
    if (s_resp_buf) { lv_free(s_resp_buf); s_resp_buf = NULL; }
}
bool lvv_spy_is_connected(void) { return false; }

#endif /* LVV_USE_POSIX_SOCKETS */
