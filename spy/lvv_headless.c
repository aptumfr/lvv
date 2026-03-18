/**
 * @file lvv_headless.c
 * @brief Headless LVGL display driver for CI/testing
 */

#include "lvv_headless.h"
#include "lvv_spy.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static void headless_flush_cb(lv_display_t *disp, const lv_area_t *area,
                               uint8_t *px_map) {
    (void)area;
    (void)px_map;
    lv_display_flush_ready(disp);
}

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

lv_display_t* lvv_headless_create(int width, int height) {
    /* Allocate draw buffer first so we can fail fast */
    size_t buf_size = (size_t)width * 60 * 4;  /* 60 rows, ARGB8888 */
    void *buf = malloc(buf_size);
    if (!buf) {
        fprintf(stderr, "[lvv_headless] Failed to allocate %zu byte draw buffer\n",
                buf_size);
        return NULL;
    }

    lv_display_t *disp = lv_display_create(width, height);
    if (!disp) {
        fprintf(stderr, "[lvv_headless] lv_display_create() failed\n");
        free(buf);
        return NULL;
    }
    lv_display_set_flush_cb(disp, headless_flush_cb);
    lv_display_set_buffers(disp, buf, NULL, buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    return disp;
}

void lvv_headless_run(void) {
    uint32_t prev = now_ms();
    while (1) {
        uint32_t cur = now_ms();
        uint32_t elapsed = cur - prev;
        prev = cur;
        if (elapsed > 0) lv_tick_inc(elapsed);
        lv_timer_handler();
        lvv_spy_process();
        usleep(5000);
    }
}
