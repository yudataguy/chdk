// Host unit tests for core/battsave.c
#include <stdio.h>
#include "fake_hw.h"

static int failures;
static int checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define MIN_MS  (60u * 1000u)
#define HOUR_MS (60u * MIN_MS)
#define BS_LENS_SETTLE_MS_EXPECTED 1000u   // wait after rec mode returns before restoring zoom

static battsave_config_t cfg_default(void)
{
    battsave_config_t c;
    c.enabled = 1;
    c.t1_ms = 30000;
    c.t2_ms = 180000;
    c.t3_ms = 7200000;
    c.blink = 1;
    c.deep_allowed = 1;
    c.zoom_allowed = 1;
    c.low_batt_mv = 0;
    return c;
}

static unsigned now;

// tick every 20 ms (the spytask period) from now until now+duration inclusive
static void run_for(unsigned duration)
{
    unsigned start = now;
    for (;;) {
        battsave_tick(now);
        if (now - start >= duration) break;
        now += 20;
    }
}

static void tick_at(unsigned offset_from_start, unsigned start)
{
    now = start + offset_from_start;
    battsave_tick(now);
}

static void setup(battsave_config_t c, unsigned start)
{
    fake_reset();
    now = start;
    battsave_init(&fake_hw_ops, &c, now);
}

// ---------------------------------------------------------------- AWAKE/LIGHT

static void test_disabled_never_sleeps(void)
{
    battsave_config_t c = cfg_default();
    c.enabled = 0;
    setup(c, 0);
    run_for(10 * HOUR_MS);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.display_on == 1);
    CHECK(battsave_overlays_paused() == 0);
}

static void test_light_after_t1(void)
{
    setup(cfg_default(), 0);
    run_for(29980);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.display_on == 1);
    tick_at(30000, 0);
    CHECK(battsave_state() == BS_LIGHT);
    CHECK(fake.display_on == 0);
    CHECK(battsave_overlays_paused() == 1);
}

static void test_light_key_passes_and_wakes(void)
{
    setup(cfg_default(), 0);
    run_for(31000);
    CHECK(battsave_state() == BS_LIGHT);
    CHECK(battsave_key(1, now) == 0);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.display_on == 1);
    CHECK(battsave_overlays_paused() == 0);
}

static void test_activity_resets_idle(void)
{
    setup(cfg_default(), 0);
    run_for(20000);
    CHECK(battsave_key(1, now) == 0);
    now += 20;
    battsave_key(0, now);
    run_for(25000 - 20);            // now = 45000
    CHECK(battsave_state() == BS_AWAKE);
    tick_at(49980, 0);
    CHECK(battsave_state() == BS_AWAKE);
    tick_at(50000, 0);
    CHECK(battsave_state() == BS_LIGHT);
}

static void test_inhibit_blocks_sleep(void)
{
    setup(cfg_default(), 0);
    fake.inhibit = 1;
    run_for(10 * MIN_MS);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.display_on == 1);

    setup(cfg_default(), 0);
    run_for(31000);
    CHECK(battsave_state() == BS_LIGHT);
    fake.inhibit = 1;
    run_for(20);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.display_on == 1);
}

static void test_user_playback_blocks_sleep(void)
{
    setup(cfg_default(), 0);
    fake.rec_mode = 0;
    run_for(10 * MIN_MS);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.display_on == 1);
    CHECK(fake.set_rec_calls == 0);
}

static void test_light_sleep_across_tick_wrap(void)
{
    unsigned start = 0xFFFFFFFFu - 10000u;
    setup(cfg_default(), start);
    run_for(29980);
    CHECK(battsave_state() == BS_AWAKE);
    tick_at(30000, start);
    CHECK(battsave_state() == BS_LIGHT);
}

// ---------------------------------------------------------------- DEEP

// default config, idle until DEEP; returns the time DEEP was entered
static unsigned go_deep(battsave_config_t c)
{
    setup(c, 0);
    fake.zoom = 42;
    run_for(c.t2_ms > c.t1_ms ? c.t2_ms : c.t1_ms);
    return now;
}

static void test_deep_after_t2(void)
{
    setup(cfg_default(), 0);
    fake.zoom = 42;
    run_for(179980);
    CHECK(battsave_state() == BS_LIGHT);
    tick_at(180000, 0);
    CHECK(battsave_state() == BS_DEEP);
    CHECK(fake.rec_mode == 0);
    CHECK(fake.set_rec_calls == 1);
    CHECK(fake.get_zoom_calls >= 1);
    CHECK(fake.display_on == 0);
    CHECK(battsave_overlays_paused() == 1);
}

static void test_t2_never_stays_light(void)
{
    battsave_config_t c = cfg_default();
    c.t2_ms = BS_NEVER;
    setup(c, 0);
    run_for(10 * HOUR_MS);
    CHECK(battsave_state() == BS_LIGHT);
    CHECK(fake.set_rec_calls == 0);
}

static void test_t2_le_t1_goes_straight_deep(void)
{
    battsave_config_t c = cfg_default();
    c.t1_ms = 60000;
    c.t2_ms = 30000;
    setup(c, 0);
    int saw_light = 0;
    while (now < 59980) {
        battsave_tick(now);
        if (battsave_state() == BS_LIGHT) saw_light = 1;
        now += 20;
    }
    battsave_tick(now);
    CHECK(battsave_state() == BS_AWAKE);
    tick_at(60000, 0);
    CHECK(battsave_state() == BS_DEEP);
    CHECK(fake.display_on == 0);
    CHECK(saw_light == 0);
}

static void test_deep_not_allowed(void)
{
    battsave_config_t c = cfg_default();
    c.deep_allowed = 0;
    setup(c, 0);
    run_for(10 * HOUR_MS);
    CHECK(battsave_state() == BS_LIGHT);
    CHECK(fake.set_rec_calls == 0);
}

// LED on-time in ms over one 3000 ms window, starting 3000 ms after DEEP entry
static unsigned led_on_ms_in_window(void)
{
    unsigned on = 0;
    unsigned start = now + 3000;
    run_for(3000 - 20);     // now = deep + 2980
    for (now = start; now < start + 3000; now += 20) {
        battsave_tick(now);
        if (fake.led_on) on += 20;
    }
    return on;
}

static void test_deep_blink_pattern(void)
{
    go_deep(cfg_default());
    CHECK(battsave_state() == BS_DEEP);
    unsigned on = led_on_ms_in_window();
    CHECK(on >= 40 && on <= 60);
    CHECK(battsave_state() == BS_DEEP);

    battsave_config_t c = cfg_default();
    c.blink = 0;
    go_deep(c);
    CHECK(led_on_ms_in_window() == 0);
}

static void test_deep_wake_restores(void)
{
    go_deep(cfg_default());
    run_for(1000);
    CHECK(battsave_key(1, now) == 1);
    CHECK(battsave_state() == BS_WAKING);
    CHECK(fake.set_rec_calls == 2);
    run_for(BS_LENS_SETTLE_MS_EXPECTED + 20);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.display_on == 1);
    CHECK(fake.led_on == 1);
    CHECK(fake.set_zoom_last == 42);
    CHECK(battsave_overlays_paused() == 0);
}

static void test_deep_wake_swallows_until_release(void)
{
    go_deep(cfg_default());
    run_for(1000);
    CHECK(battsave_key(1, now) == 1);
    unsigned end = now + 2000;
    int all_swallowed = 1;
    while (now < end) {
        now += 20;
        battsave_tick(now);
        if (battsave_key(1, now) != 1) all_swallowed = 0;
    }
    CHECK(all_swallowed);
    CHECK(battsave_state() == BS_AWAKE);
    now += 20;
    battsave_key(0, now);
    now += 20;
    CHECK(battsave_key(1, now) == 0);
}

static void test_zoom_skip_if_moved(void)
{
    go_deep(cfg_default());
    fake.rec_mode_follows = 0;
    battsave_key(1, now);
    CHECK(battsave_state() == BS_WAKING);
    run_for(500);
    fake.zoom = 10;         // user zoomed while the lens was extending
    fake.rec_mode = 1;
    run_for(BS_LENS_SETTLE_MS_EXPECTED + 20);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.set_zoom_calls == 0);
}

static void test_zoom_disallowed(void)
{
    battsave_config_t c = cfg_default();
    c.zoom_allowed = 0;
    go_deep(c);
    battsave_key(1, now);
    run_for(BS_LENS_SETTLE_MS_EXPECTED + 100);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.get_zoom_calls == 0);
    CHECK(fake.set_zoom_calls == 0);
}

static void test_wake_retry_then_fail(void)
{
    go_deep(cfg_default());
    fake.rec_mode_follows = 0;
    unsigned w = now;
    battsave_key(1, w);
    run_for(4980);
    CHECK(battsave_state() == BS_WAKING);
    tick_at(5000, w);
    CHECK(battsave_state() == BS_WAKE_FAILED);
    CHECK(fake.display_on == 1);
    CHECK(battsave_overlays_paused() == 0);
    int led_a = fake.led_on;
    tick_at(5250, w);
    int led_b = fake.led_on;
    tick_at(5500, w);
    int led_c = fake.led_on;
    CHECK(led_a != led_b);
    CHECK(led_b != led_c);
    battsave_key(0, now);
    now += 20;
    CHECK(battsave_key(1, now) == 0);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.led_on == 1);
}

// ---------------------------------------------------------------- safety

static void test_power_off_after_t3(void)
{
    unsigned d = go_deep(cfg_default());
    CHECK(d == 180000);
    run_for(7200000 - 20);
    tick_at(7199999, d);
    CHECK(fake.powered_off == 0);
    CHECK(battsave_state() == BS_DEEP);
    tick_at(7200000, d);
    CHECK(fake.powered_off == 1);
    CHECK(battsave_state() == BS_POWER_OFF);
}

static void test_t3_never(void)
{
    battsave_config_t c = cfg_default();
    c.t3_ms = BS_NEVER;
    go_deep(c);
    run_for(48 * HOUR_MS);
    CHECK(fake.powered_off == 0);
    CHECK(battsave_state() == BS_DEEP);
}

static void test_low_battery_check_off(void)
{
    battsave_config_t c = cfg_default();
    c.low_batt_mv = 0;
    go_deep(c);
    fake.vbatt = 3000;
    run_for(10 * MIN_MS);
    CHECK(fake.powered_off == 0);
}

static void test_low_battery_ignored_when_awake(void)
{
    battsave_config_t c = cfg_default();
    c.low_batt_mv = 3300;
    setup(c, 0);
    fake.vbatt = 3000;
    run_for(10000);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.powered_off == 0);
}

static void test_inhibit_in_deep_wakes(void)
{
    go_deep(cfg_default());
    fake.rec_mode_follows = 0;
    fake.inhibit = 1;
    run_for(20);
    CHECK(battsave_state() == BS_WAKING);
    CHECK(fake.set_rec_calls == 2);
}

static void test_disable_while_asleep_restores(void)
{
    battsave_config_t off = cfg_default();
    off.enabled = 0;

    setup(cfg_default(), 0);
    run_for(31000);
    CHECK(battsave_state() == BS_LIGHT);
    battsave_set_config(&off, now);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.display_on == 1);
    CHECK(fake.led_on == 1);

    go_deep(cfg_default());
    battsave_set_config(&off, now);
    CHECK(battsave_state() == BS_WAKING);
    run_for(BS_LENS_SETTLE_MS_EXPECTED + 20);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.display_on == 1);
    CHECK(fake.led_on == 1);
    CHECK(fake.set_zoom_last == 42);
    run_for(HOUR_MS);
    CHECK(battsave_state() == BS_AWAKE);
}

static void test_config_from_menu(void)
{
    battsave_config_t c = cfg_default();
    battsave_config_from_menu(&c, 1, BS_T1_DEFAULT_IDX, BS_T2_DEFAULT_IDX, BS_T3_DEFAULT_IDX, 1);
    CHECK(c.enabled == 1);
    CHECK(c.t1_ms == 30000);
    CHECK(c.t2_ms == 180000);
    CHECK(c.t3_ms == 7200000);
    CHECK(c.blink == 1);

    battsave_config_from_menu(&c, 0, 0, 4, 4, 0);
    CHECK(c.enabled == 0);
    CHECK(c.t1_ms == 10000);
    CHECK(c.t2_ms == BS_NEVER);
    CHECK(c.t3_ms == BS_NEVER);
    CHECK(c.blink == 0);

    battsave_config_from_menu(&c, 1, 3, 3, 3, 1);
    CHECK(c.t1_ms == 120000);
    CHECK(c.t2_ms == 600000);
    CHECK(c.t3_ms == 4 * HOUR_MS);

    battsave_config_from_menu(&c, 1, 9, -1, 99, 1);
    CHECK(c.t1_ms == 30000);
    CHECK(c.t2_ms == 180000);
    CHECK(c.t3_ms == 7200000);
}

// ---------------------------------------------------------------- review fixes

static void test_zoom_restore_waits_for_lens(void)
{
    go_deep(cfg_default());
    battsave_key(1, now);
    run_for(20);                    // rec mode already back (fake follows)
    CHECK(battsave_state() == BS_WAKING);
    CHECK(fake.display_on == 1);    // screen comes back as soon as rec mode does
    CHECK(fake.set_zoom_calls == 0);
    run_for(BS_LENS_SETTLE_MS_EXPECTED - 60);
    CHECK(fake.set_zoom_calls == 0);
    run_for(60);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.set_zoom_last == 42);
}

static void test_prevent_shutdown_owned_in_rec(void)
{
    setup(cfg_default(), 0);
    run_for(20);
    CHECK(fake.prevent_shutdown == 1);
    run_for(200000);                // LIGHT then DEEP: still owned
    CHECK(battsave_state() == BS_DEEP);
    CHECK(fake.prevent_shutdown == 1);
}

static void test_canon_autooff_in_user_playback(void)
{
    setup(cfg_default(), 0);
    fake.rec_mode = 0;
    run_for(20);
    CHECK(fake.prevent_shutdown == 0);
    fake.rec_mode = 1;
    run_for(20);
    CHECK(fake.prevent_shutdown == 1);
}

static void test_canon_autooff_when_no_deep(void)
{
    battsave_config_t c = cfg_default();
    c.t2_ms = BS_NEVER;
    setup(c, 0);
    run_for(HOUR_MS);
    CHECK(battsave_state() == BS_LIGHT);
    CHECK(fake.prevent_shutdown == 0);

    c = cfg_default();
    c.deep_allowed = 0;
    setup(c, 0);
    run_for(20);
    CHECK(fake.prevent_shutdown == 0);
}

static void test_canon_autooff_after_wake_failed(void)
{
    go_deep(cfg_default());
    fake.rec_mode_follows = 0;
    battsave_key(1, now);
    run_for(5000);
    CHECK(battsave_state() == BS_WAKE_FAILED);
    CHECK(fake.prevent_shutdown == 0);
}

static void test_disable_releases_shutdown(void)
{
    battsave_config_t off = cfg_default();
    off.enabled = 0;
    setup(cfg_default(), 0);
    run_for(20);
    CHECK(fake.prevent_shutdown == 1);
    battsave_set_config(&off, now);
    CHECK(fake.prevent_shutdown == 0);

    setup(off, 0);
    CHECK(fake.prevent_shutdown == 0);
}

static void test_enable_later_resets_idle(void)
{
    battsave_config_t off = cfg_default();
    off.enabled = 0;
    battsave_config_t on = cfg_default();
    setup(off, 0);
    run_for(10 * MIN_MS);
    battsave_set_config(&on, now);
    run_for(20);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.set_rec_calls == 0);
    unsigned enabled_at = now - 20;
    tick_at(29980, enabled_at);
    CHECK(battsave_state() == BS_AWAKE);
    tick_at(30000, enabled_at);
    CHECK(battsave_state() == BS_LIGHT);
}

static void test_key_stamp_ahead_of_tick(void)
{
    // keyboard task stamps activity with a newer tick than the spytask's
    setup(cfg_default(), 0);
    run_for(5000);
    battsave_key(1, 5010);
    battsave_tick(5000);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.set_rec_calls == 0);
    CHECK(fake.display_on == 1);
}

// ------------------------------------------------------- dial / mode dial

static void test_dial_keeps_awake(void)
{
    setup(cfg_default(), 0);
    run_for(20000);
    fake.activity = 1;
    run_for(29980);                 // 49980 total, 29980 since the dial moved
    CHECK(battsave_state() == BS_AWAKE);
    run_for(40);
    CHECK(battsave_state() == BS_LIGHT);
}

static void test_dial_wakes_light(void)
{
    setup(cfg_default(), 0);
    run_for(30000);
    CHECK(battsave_state() == BS_LIGHT);
    fake.activity = 1;
    run_for(20);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.display_on == 1);
}

static void test_dial_wakes_deep(void)
{
    go_deep(cfg_default());
    CHECK(battsave_state() == BS_DEEP);
    fake.activity = 1;
    run_for(20);
    CHECK(battsave_state() == BS_WAKING);
    CHECK(fake.set_rec_calls == 2);
    CHECK(battsave_key(0, now) == 0);   // nothing to swallow: no key was pressed
    run_for(BS_LENS_SETTLE_MS_EXPECTED + 40);
    CHECK(battsave_state() == BS_AWAKE);
}

static void test_dial_wakes_failed(void)
{
    go_deep(cfg_default());
    fake.rec_mode_follows = 0;
    fake.activity = 1;
    run_for(6000);
    CHECK(battsave_state() == BS_WAKE_FAILED);
    fake.activity = 1;
    run_for(20);
    CHECK(battsave_state() == BS_AWAKE);
    CHECK(fake.led_on == 1);
}

static void test_dial_ignored_when_disabled(void)
{
    battsave_config_t c = cfg_default();
    c.enabled = 0;
    setup(c, 0);
    fake.activity = 1;
    run_for(20);
    CHECK(battsave_state() == BS_AWAKE);
}

// ------------------------------------------------------- low battery smoothing

#define VBATT_CHECK_MS_EXPECTED 10000u
#define VBATT_LOW_COUNT_EXPECTED 3

static void test_low_battery_three_checks(void)
{
    battsave_config_t c = cfg_default();
    c.low_batt_mv = 3300;
    unsigned deep_at = go_deep(c);
    fake.vbatt = 3299;
    run_for(VBATT_CHECK_MS_EXPECTED * VBATT_LOW_COUNT_EXPECTED - 20);
    CHECK(fake.powered_off == 0);
    tick_at(VBATT_CHECK_MS_EXPECTED * VBATT_LOW_COUNT_EXPECTED, deep_at);
    CHECK(fake.powered_off == 1);
    CHECK(battsave_state() == BS_POWER_OFF);
}

static void test_low_battery_single_dip_ignored(void)
{
    battsave_config_t c = cfg_default();
    c.low_batt_mv = 3300;
    unsigned deep_at = go_deep(c);
    run_for(VBATT_CHECK_MS_EXPECTED - 100);
    fake.vbatt = 3000;              // one low reading at the next check...
    run_for(200);
    fake.vbatt = 4000;              // ...then back to normal
    now = deep_at;
    run_for(10 * MIN_MS);
    CHECK(fake.powered_off == 0);
    CHECK(battsave_state() == BS_DEEP);
}

static void test_low_battery_recovery_resets_count(void)
{
    battsave_config_t c = cfg_default();
    c.low_batt_mv = 3300;
    unsigned deep_at = go_deep(c);
    fake.vbatt = 3000;
    run_for(2 * VBATT_CHECK_MS_EXPECTED);           // two lows
    fake.vbatt = 4000;
    run_for(VBATT_CHECK_MS_EXPECTED);               // one normal: count resets
    fake.vbatt = 3000;
    run_for(2 * VBATT_CHECK_MS_EXPECTED);           // two lows again
    CHECK(fake.powered_off == 0);
    run_for(VBATT_CHECK_MS_EXPECTED);               // third consecutive low
    CHECK(fake.powered_off == 1);
    (void)deep_at;
}

// ------------------------------------------------------- card log

#define LOG_PERIOD_MS_EXPECTED (10u * MIN_MS)

static void test_log_on_state_change(void)
{
    setup(cfg_default(), 1000);
    CHECK(fake.log_count == 1);
    CHECK(fake.log_state[0] == BS_AWAKE);
    CHECK(fake.log_ms[0] == 1000);
    run_for(30000);
    CHECK(fake.log_count == 2);
    CHECK(fake.log_state[1] == BS_LIGHT);
    CHECK(fake.log_ms[1] == 31000);
    battsave_key(1, now);
    CHECK(fake.log_count == 3);
    CHECK(fake.log_state[2] == BS_AWAKE);
    battsave_key(0, now);
    run_for(60000);                 // no state changes beyond LIGHT again
    CHECK(fake.log_count == 4);
}

static void test_log_periodic_while_asleep(void)
{
    battsave_config_t c = cfg_default();
    c.t3_ms = BS_NEVER;
    unsigned deep_at = go_deep(c);
    int base = fake.log_count;
    CHECK(fake.log_state[base - 1] == BS_DEEP);
    run_for(LOG_PERIOD_MS_EXPECTED - 20);
    CHECK(fake.log_count == base);
    run_for(20);
    CHECK(fake.log_count == base + 1);
    CHECK(fake.log_state[base] == BS_DEEP);
    CHECK(fake.log_ms[base] == deep_at + LOG_PERIOD_MS_EXPECTED);
    run_for(LOG_PERIOD_MS_EXPECTED);
    CHECK(fake.log_count == base + 2);
}

static void test_log_not_periodic_when_awake(void)
{
    setup(cfg_default(), 0);
    fake.inhibit = 1;
    run_for(3 * LOG_PERIOD_MS_EXPECTED);
    CHECK(fake.log_count == 1);
}

static void test_log_before_power_off(void)
{
    battsave_config_t c = cfg_default();
    c.t3_ms = 30u * MIN_MS;
    go_deep(c);
    run_for(30u * MIN_MS);
    CHECK(fake.powered_off == 1);
    CHECK(fake.log_state[fake.log_count - 1] == BS_POWER_OFF);
}

int main(void)
{
    test_dial_keeps_awake();
    test_dial_wakes_light();
    test_dial_wakes_deep();
    test_dial_wakes_failed();
    test_dial_ignored_when_disabled();
    test_low_battery_three_checks();
    test_low_battery_single_dip_ignored();
    test_low_battery_recovery_resets_count();
    test_log_on_state_change();
    test_log_periodic_while_asleep();
    test_log_not_periodic_when_awake();
    test_log_before_power_off();

    test_zoom_restore_waits_for_lens();
    test_prevent_shutdown_owned_in_rec();
    test_canon_autooff_in_user_playback();
    test_canon_autooff_when_no_deep();
    test_canon_autooff_after_wake_failed();
    test_disable_releases_shutdown();
    test_enable_later_resets_idle();
    test_key_stamp_ahead_of_tick();

    test_power_off_after_t3();
    test_t3_never();
    test_low_battery_check_off();
    test_low_battery_ignored_when_awake();
    test_inhibit_in_deep_wakes();
    test_disable_while_asleep_restores();
    test_config_from_menu();

    test_deep_after_t2();
    test_t2_never_stays_light();
    test_t2_le_t1_goes_straight_deep();
    test_deep_not_allowed();
    test_deep_blink_pattern();
    test_deep_wake_restores();
    test_deep_wake_swallows_until_release();
    test_zoom_skip_if_moved();
    test_zoom_disallowed();
    test_wake_retry_then_fail();

    test_disabled_never_sleeps();
    test_light_after_t1();
    test_light_key_passes_and_wakes();
    test_activity_resets_idle();
    test_inhibit_blocks_sleep();
    test_user_playback_blocks_sleep();
    test_light_sleep_across_tick_wrap();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
