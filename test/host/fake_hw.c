#include "fake_hw.h"

fake_hw_t fake;

static void f_display(int on)       { fake.display_on = on; }
static void f_led(int on)           { fake.led_on = on; }
static int  f_is_rec_mode(void)     { return fake.rec_mode; }
// leaving rec mode retracts the lens, which resets zoom to wide (0)
static void f_set_rec_mode(int rec) { fake.set_rec_calls++; if (!rec) fake.zoom = 0; if (fake.rec_mode_follows) fake.rec_mode = rec; }
static int  f_get_zoom(void)        { fake.get_zoom_calls++; return fake.zoom; }
static void f_set_zoom(int z)       { fake.set_zoom_calls++; fake.set_zoom_last = z; fake.zoom = z; }
static void f_power_off(void)       { fake.powered_off = 1; }
static int  f_vbatt_mv(void)        { return fake.vbatt; }
static int  f_inhibit(void)         { return fake.inhibit; }

const battsave_hw_t fake_hw_ops = {
    f_display, f_led, f_is_rec_mode, f_set_rec_mode,
    f_get_zoom, f_set_zoom, f_power_off, f_vbatt_mv, f_inhibit
};

void fake_reset(void)
{
    fake = (fake_hw_t){0};
    fake.display_on = 1;
    fake.led_on = 1;
    fake.rec_mode = 1;
    fake.vbatt = 4000;
    fake.rec_mode_follows = 1;
    fake.set_zoom_last = -1;
}
