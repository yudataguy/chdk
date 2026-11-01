// CHDK Battery Saver state machine. Platform independent: no camera calls,
// no clock reads, no libc. See include/battsave.h.
#include "battsave.h"

static const battsave_hw_t *hw;
static battsave_config_t    cfg;
static battsave_state_t     state;
static unsigned             last_activity_ms;   // last time a key was down (or sleep was inhibited)
static unsigned             state_since_ms;

static void set_state(battsave_state_t s, unsigned now_ms)
{
    state = s;
    state_since_ms = now_ms;
}

static void enter_light(unsigned now_ms)
{
    hw->display(0);
    set_state(BS_LIGHT, now_ms);
}

static void wake_light(unsigned now_ms)
{
    hw->display(1);
    last_activity_ms = now_ms;
    set_state(BS_AWAKE, now_ms);
}

void battsave_init(const battsave_hw_t *h, const battsave_config_t *c, unsigned now_ms)
{
    hw = h;
    cfg = *c;
    last_activity_ms = now_ms;
    set_state(BS_AWAKE, now_ms);
}

void battsave_set_config(const battsave_config_t *c, unsigned now_ms)
{
    (void)now_ms;
    cfg = *c;
}

// sleep must not start (or continue) while the camera is busy, or while the
// user is in playback by their own choice
static int awake_inhibited(void)
{
    return hw->inhibit() || !hw->is_rec_mode();
}

void battsave_tick(unsigned now_ms)
{
    if (!hw || !cfg.enabled)
        return;

    switch (state) {
    case BS_AWAKE:
        if (awake_inhibited())
            last_activity_ms = now_ms;
        else if (now_ms - last_activity_ms >= cfg.t1_ms)
            enter_light(now_ms);
        break;
    case BS_LIGHT:
        if (awake_inhibited())
            wake_light(now_ms);
        break;
    default:
        break;
    }
}

int battsave_key(int any_key_down, unsigned now_ms)
{
    if (!hw || !cfg.enabled)
        return 0;
    if (any_key_down) {
        last_activity_ms = now_ms;
        if (state == BS_LIGHT)
            wake_light(now_ms);
    }
    return 0;
}

battsave_state_t battsave_state(void)
{
    return state;
}

int battsave_overlays_paused(void)
{
    return state == BS_LIGHT || state == BS_DEEP || state == BS_WAKING;
}

int battsave_enabled(void)
{
    return cfg.enabled;
}
