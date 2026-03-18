#ifndef LVV_HEADLESS_H
#define LVV_HEADLESS_H

/**
 * @file lvv_headless.h
 * @brief Headless LVGL display driver for CI/testing
 *
 * Creates an LVGL display with no actual output. LVGL's draw engine
 * still works (the spy uses it for screenshots), but nothing is
 * rendered to screen. No X11, SDL, or framebuffer needed.
 *
 * Usage:
 *   #include "lvv_headless.h"
 *   #include "lvv_spy.h"
 *
 *   int main() {
 *       lv_init();
 *       lvv_headless_create(800, 480);
 *       build_ui();
 *       lvv_spy_init(5555);
 *       lvv_headless_run();  // blocks, processing LVGL + spy
 *   }
 */

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Create a headless display (no-op flush, no hardware needed) */
lv_display_t* lvv_headless_create(int width, int height);

/**
 * Run the LVGL + spy main loop (blocks forever).
 * Calls lv_timer_handler() and lvv_spy_process() at ~200Hz.
 * Requires lvv_spy_init() to have been called first.
 */
void lvv_headless_run(void);

#ifdef __cplusplus
}
#endif

#endif /* LVV_HEADLESS_H */
