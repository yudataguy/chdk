// Battery Saver camera binding: connects the platform independent state
// machine (core/battsave.c) to CHDK camera functions.
#include "camera.h"

#ifdef CAM_HAS_BATTSAVE

#include "platform.h"
#include "conf.h"
#include "clock.h"
#include "shooting.h"
#include "backlight.h"
#include "battery.h"
#include "shutdown.h"
#include "debug_led.h"
#include "usb_remote.h"
#include "battsave.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"

static void hw_display(int on)
{
    if (on) {
        TurnOnDisplay();
        TurnOnBackLight();
    } else {
        TurnOffBackLight();
        TurnOffDisplay();
    }
}

static void hw_led(int on)
{
#ifdef CAM_BATTSAVE_LED
    camera_set_led(CAM_BATTSAVE_LED, on, 0);
#else
    (void)on;
#endif
}

static int  hw_is_rec_mode(void)       { return camera_info.state.mode_rec; }
static void hw_set_rec_mode(int rec)   { shooting_set_playrec_mode(rec); }
static int  hw_get_zoom(void)          { return shooting_get_zoom(); }
static void hw_set_zoom(int z)         { shooting_set_zoom(z); }
static void hw_power_off(void)         { shutdown(); }
static int  hw_vbatt_mv(void)          { return (int)stat_get_vbatt(); }

static int hw_inhibit(void)
{
    return camera_info.state.state_kbd_script_run
        || !camera_info.state.gui_mode_none     // CHDK menu / ALT mode open
        || is_video_recording()
        || get_usb_bit();
}

static int prevent_shutdown_wanted;

static void hw_prevent_shutdown(int on)
{
    prevent_shutdown_wanted = on;
    conf_update_prevent_shutdown();
}

int battsave_hw_prevent_shutdown(void)
{
    return prevent_shutdown_wanted;
}

// Dial turns and mode dial changes count as activity. The raw dial counter is
// read directly: get_jogdial_direction() consumes the movement and would steal
// it from CHDK's own menus.
#ifdef CAM_BATTSAVE_DIAL_POS
extern short CAM_BATTSAVE_DIAL_POS;
#endif

static int hw_activity_poll(void)
{
    static int primed;
    static int last_mode;
    int moved = 0;
#ifdef CAM_BATTSAVE_DIAL_POS
    static short last_dial;
    short dial = CAM_BATTSAVE_DIAL_POS;
    if (primed && dial != last_dial)
        moved = 1;
    last_dial = dial;
#endif
    // only while shooting: entering deep sleep switches to playback, which
    // must not look like a mode change and wake the camera straight back up
    if (camera_info.state.mode_rec) {
        int mode = camera_info.state.mode_shooting;
        if (primed && mode != last_mode)
            moved = 1;
        last_mode = mode;
    }
    primed = 1;
    return moved;
}

static const char *const state_names[] = { "awake", "light", "deep", "waking", "wake_failed", "power_off" };

static void hw_log_event(battsave_state_t s, unsigned now_ms)
{
    char buf[64];
    int fd;
    if (!conf.battsave_log)
        return;
    fd = open("A/BATTSAVE.LOG", O_WRONLY|O_CREAT|O_APPEND, 0777);
    if (fd < 0)
        return;
    lseek(fd, 0, SEEK_END);     // O_APPEND is ignored on VxWorks
    sprintf(buf, "%d,%s,%d\n", (int)now_ms,
            ((unsigned)s < sizeof(state_names) / sizeof(state_names[0])) ? state_names[s] : "?",
            (int)stat_get_vbatt());
    write(fd, buf, strlen(buf));
    close(fd);
}

static const battsave_hw_t battsave_hw = {
    hw_display, hw_led, hw_is_rec_mode, hw_set_rec_mode,
    hw_get_zoom, hw_set_zoom, hw_power_off, hw_vbatt_mv, hw_inhibit,
    hw_prevent_shutdown, hw_activity_poll, hw_log_event
};

static void config_from_conf(battsave_config_t *c)
{
    battsave_config_from_menu(c, conf.battsave_enable, conf.battsave_t1, conf.battsave_t2,
                              conf.battsave_t3, conf.battsave_blink);
#ifndef CAM_BATTSAVE_LED
    c->blink = 0;
#endif
#ifdef CAM_BATTSAVE_NO_DEEP
    c->deep_allowed = 0;
#else
    c->deep_allowed = 1;
#endif
#if CAM_HAS_ZOOM_LEVER
    c->zoom_allowed = 1;
#else
    c->zoom_allowed = 0;
#endif
    c->low_batt_mv = conf.batt_volts_min;
}

void battsave_hw_init(void)
{
    battsave_config_t c;
    config_from_conf(&c);
    battsave_init(&battsave_hw, &c, (unsigned)get_tick_count());
}

void battsave_hw_apply_conf(void)
{
    battsave_config_t c;
    config_from_conf(&c);
    battsave_set_config(&c, (unsigned)get_tick_count());
    conf_update_prevent_shutdown();
}

#endif // CAM_HAS_BATTSAVE
