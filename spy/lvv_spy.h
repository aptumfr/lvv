#ifndef LVV_SPY_H
#define LVV_SPY_H

/**
 * @file lvv_spy.h
 * @brief LVV Spy - Embeddable LVGL introspection and remote control server
 *
 * Add this to any LVGL application to enable remote test automation.
 *
 * Usage:
 *   #include "lvv_spy.h"
 *
 *   int main() {
 *       lv_init();
 *       // ... create display, UI ...
 *
 *       lvv_spy_init(5555);  // Start spy on TCP port 5555
 *
 *       while (true) {
 *           lv_timer_handler();
 *           lvv_spy_process();  // Process spy commands (non-blocking)
 *           usleep(5000);
 *       }
 *   }
 *
 * Or with lv:: C++ wrapper:
 *   lvv_spy_init(5555);
 *   lv::run_with([&]() { lvv_spy_process(); return true; });
 */

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the spy server on the given TCP port */
bool lvv_spy_init(uint16_t port);

/** Process incoming commands (non-blocking, call from main loop) */
void lvv_spy_process(void);

/** Shut down the spy server */
void lvv_spy_deinit(void);

/** Check if a client is connected */
bool lvv_spy_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* LVV_SPY_H */
