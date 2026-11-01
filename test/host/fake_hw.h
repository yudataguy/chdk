#ifndef FAKE_HW_H
#define FAKE_HW_H

#include "battsave.h"

// Recording fake of the camera hardware for host tests of core/battsave.c
typedef struct {
    int display_on;
    int led_on;
    int rec_mode;
    int zoom;
    int powered_off;
    int vbatt;
    int inhibit;
    int set_rec_calls;
    int set_zoom_calls;
    int set_zoom_last;
    int get_zoom_calls;
    int rec_mode_follows;   // 1: set_rec_mode takes effect immediately
    int prevent_shutdown;   // last value passed to prevent_shutdown, -1 = never called
    int activity;           // next activity_poll() result (dial / mode dial moved); cleared when read
    int log_count;          // log_event calls
    int log_state[64];      // states logged (first 64)
    unsigned log_ms[64];
} fake_hw_t;

extern fake_hw_t fake;
extern const battsave_hw_t fake_hw_ops;

void fake_reset(void);

#endif
