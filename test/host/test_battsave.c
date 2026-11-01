// Host unit tests for core/battsave.c
#include <stdio.h>
#include "fake_hw.h"

static int failures;
static int checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define MIN_MS  (60u * 1000u)
#define HOUR_MS (60u * MIN_MS)

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

int main(void)
{
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
