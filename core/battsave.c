// CHDK Battery Saver state machine. Platform independent: no camera calls,
// no clock reads, no libc. See include/battsave.h.
#include "battsave.h"

static const battsave_hw_t *hw;
static battsave_config_t    cfg;
static battsave_state_t     state;
static unsigned             last_activity_ms;   // last time a key was down (or sleep was inhibited)
static unsigned             state_since_ms;
static int                  swallow;            // hide keys from Canon until all keys are released
static int                  saved_zoom;

#define BS_BLINK_PERIOD_MS      3000u
#define BS_BLINK_ON_MS          50u
#define BS_WAKE_TIMEOUT_MS      5000u
#define BS_FAIL_BLINK_MS        250u

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

static int deep_enabled(void)
{
    return cfg.deep_allowed && cfg.t2_ms != BS_NEVER;
}

// deep sleep starts at T2 total idle, but never before T1 (T2 <= T1 skips LIGHT)
static unsigned deep_at_ms(void)
{
    return cfg.t2_ms > cfg.t1_ms ? cfg.t2_ms : cfg.t1_ms;
}

static void enter_deep(unsigned now_ms)
{
    if (cfg.zoom_allowed)
        saved_zoom = hw->get_zoom();
    if (state == BS_AWAKE)
        hw->display(0);
    hw->led(0);
    hw->set_rec_mode(0);    // playback: sensor off, lens retracts
    set_state(BS_DEEP, now_ms);
}

static void start_wake(unsigned now_ms)
{
    hw->set_rec_mode(1);
    set_state(BS_WAKING, now_ms);
}

static void finish_wake(unsigned now_ms)
{
    // after the lens extends it sits at wide; if it is anywhere else the
    // user already zoomed, so leave it alone
    if (cfg.zoom_allowed && saved_zoom != 0 && hw->get_zoom() == 0)
        hw->set_zoom(saved_zoom);
    hw->display(1);
    hw->led(1);
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

static void power_off(unsigned now_ms)
{
    set_state(BS_POWER_OFF, now_ms);
    hw->power_off();
}

void battsave_set_config(const battsave_config_t *c, unsigned now_ms)
{
    cfg = *c;
    if (cfg.enabled || !hw)
        return;
    // turned off while asleep: bring the camera back now
    switch (state) {
    case BS_LIGHT:
        wake_light(now_ms);
        hw->led(1);
        break;
    case BS_DEEP:
        start_wake(now_ms);
        break;
    case BS_WAKE_FAILED:
        hw->led(1);
        set_state(BS_AWAKE, now_ms);
        break;
    default:
        break;
    }
}

// sleep must not start (or continue) while the camera is busy, or while the
// user is in playback by their own choice
static int awake_inhibited(void)
{
    return hw->inhibit() || !hw->is_rec_mode();
}

void battsave_tick(unsigned now_ms)
{
    if (!hw)
        return;
    // when disabled, only finish a wake that is already in progress
    if (!cfg.enabled && state != BS_WAKING)
        return;

    switch (state) {
    case BS_AWAKE:
        if (awake_inhibited())
            last_activity_ms = now_ms;
        else if (deep_enabled() && now_ms - last_activity_ms >= deep_at_ms())
            enter_deep(now_ms);
        else if (now_ms - last_activity_ms >= cfg.t1_ms)
            enter_light(now_ms);
        break;
    case BS_LIGHT:
        if (awake_inhibited())
            wake_light(now_ms);
        else if (deep_enabled() && now_ms - last_activity_ms >= deep_at_ms())
            enter_deep(now_ms);
        break;
    case BS_DEEP:
        if (hw->inhibit()) {
            start_wake(now_ms);
            break;
        }
        if ((cfg.t3_ms != BS_NEVER && now_ms - state_since_ms >= cfg.t3_ms)
            || (cfg.low_batt_mv && hw->vbatt_mv() < cfg.low_batt_mv)) {
            power_off(now_ms);
            break;
        }
        if (cfg.blink)
            hw->led((now_ms - state_since_ms) % BS_BLINK_PERIOD_MS < BS_BLINK_ON_MS);
        break;
    case BS_WAKING:
        if (hw->is_rec_mode())
            finish_wake(now_ms);
        else if (now_ms - state_since_ms >= BS_WAKE_TIMEOUT_MS) {
            hw->display(1);
            hw->led(1);             // fast blink starts "on"
            set_state(BS_WAKE_FAILED, now_ms);
        }
        break;
    case BS_WAKE_FAILED:
        hw->led(((now_ms - state_since_ms) / BS_FAIL_BLINK_MS) % 2 == 0);
        break;
    default:
        break;
    }
}

int battsave_key(int any_key_down, unsigned now_ms)
{
    if (!hw || !cfg.enabled)
        return 0;
    if (!any_key_down) {
        int hide = swallow;
        swallow = 0;
        return hide;
    }
    last_activity_ms = now_ms;
    switch (state) {
    case BS_LIGHT:
        wake_light(now_ms);         // keys pass through: a full press shoots
        break;
    case BS_DEEP:
        swallow = 1;                // camera is not ready to shoot; eat the wake press
        start_wake(now_ms);
        break;
    case BS_WAKE_FAILED:
        hw->led(1);
        set_state(BS_AWAKE, now_ms);
        break;
    default:
        break;
    }
    return swallow;
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

static const unsigned t1_table[] = { 10000u, 30000u, 60000u, 120000u };
static const unsigned t2_table[] = { 60000u, 180000u, 300000u, 600000u, BS_NEVER };
static const unsigned t3_table[] = { 1800000u, 3600000u, 7200000u, 14400000u, BS_NEVER };

#define BS_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static unsigned pick(const unsigned *table, int count, int idx, int def)
{
    return table[(idx >= 0 && idx < count) ? idx : def];
}

void battsave_config_from_menu(battsave_config_t *out, int enabled, int t1_idx, int t2_idx, int t3_idx, int blink)
{
    out->enabled = enabled != 0;
    out->t1_ms = pick(t1_table, BS_COUNT(t1_table), t1_idx, BS_T1_DEFAULT_IDX);
    out->t2_ms = pick(t2_table, BS_COUNT(t2_table), t2_idx, BS_T2_DEFAULT_IDX);
    out->t3_ms = pick(t3_table, BS_COUNT(t3_table), t3_idx, BS_T3_DEFAULT_IDX);
    out->blink = blink != 0;
}
