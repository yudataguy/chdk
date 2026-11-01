# Battery Saver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a tiered CHDK Battery Saver (screen-off → lens-retracted deep sleep → safety power-off) for the Canon SX50 HS, portable to other ports via `platform_camera.h` defines.

**Architecture:** A pure C state machine (`core/battsave.c`) that never touches hardware or clocks; it is driven by `battsave_tick(now_ms)` from the spytask loop and `battsave_key()` from keyboard processing, and acts through a `battsave_hw_t` function-pointer struct. On camera, `core/battsave_hw.c` binds that struct to existing CHDK APIs; on the Mac, `test/host/fake_hw.c` records calls so tests can replay hours of idle in microseconds.

**Tech Stack:** C (CHDK, arm-none-eabi-gcc 5.4 via `tools/docker/chdkbuild`), host `cc` for unit tests, CHDK Lua for the on-camera probe, Python 3 + pytest for probe analysis, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-01-battery-saver-design.md`

## Global Constraints

- Feature compiled only where `CAM_HAS_BATTSAVE` is defined; SX50 HS sets `CAM_HAS_BATTSAVE 1` and `CAM_BATTSAVE_LED 0`.
- Battery Saver defaults to **off**; with it off, behavior must be identical to stock CHDK.
- Defaults: T1 = 30 s, T2 = 3 min, T3 = 2 h, sleep blink on.
- Menu values: T1 ∈ {10 s, 30 s, 1 min, 2 min}; T2 ∈ {1, 3, 5, 10 min, never}; T3 ∈ {30 min, 1 h, 2 h, 4 h, never}.
- Persisted conf IDs start at 299 (highest existing is 298).
- Deep-sleep blink: LED on 50 ms every 3000 ms. Never blink the AF lamp (no `CAM_BATTSAVE_LED` → no blink).
- Rec-mode wake retry: up to 5000 ms, then give up with display on and fast blink (250 ms on/off).
- `core/battsave.c` must compile with `-nostdinc` (no libc; only `<stdint.h>`-equivalent types from CHDK headers or plain `int`/`unsigned`).
- Time arithmetic uses `unsigned` subtraction (`now - since`) so tick wraparound is harmless.
- Commits end with the attribution lines:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_01V8HBQcDjT3ujbvgAFEj138`

## Review Focus

1. **Key held across a deep-sleep wake** — user holds half-press; the press that woke the camera must stay swallowed until released, and the *next* press must reach Canon. Test in Task 3 (`test_deep_wake_swallows_until_release`).
2. **Tick counter wraparound** — `get_tick_count()` wraps; idle timing must stay correct when `now` crosses `0xFFFFFFFF`. Test in Task 2 (`test_light_sleep_across_tick_wrap`).
3. **Battery Saver disabled (menu, conf reset) while asleep** — screen and LED must come back immediately, state AWAKE. Test in Task 4 (`test_disable_while_asleep_restores`).
4. **Mode switch issued from the spytask** — `shooting_set_playrec_mode()` may misbehave outside the keyboard task. Not host-testable: Task 6 includes an explicit on-camera check step; if it fails, move the call into the kbd task via a pending-request flag polled in `kbd_process()`.
5. **Canon "Lens Retract" setting** — SX50 defaults to retracting 1 min after entering playback, not immediately. Probe (Task 1) and final check (Task 9) instructions tell the user to set it to "0 sec" and record which setting was used.

---

## File Structure

| File | Responsibility |
|---|---|
| `test/battsave_probe.lua` | Throwaway on-camera power probe (CHDK Lua script) |
| `test/analyze_probe.py`, `test/test_analyze_probe.py` | Probe CSV → mV/hour per mode + go/no-go |
| `include/battsave.h` | Public types and API of the state machine |
| `core/battsave.c` | Pure state machine (built once in `lib/core`) |
| `test/host/Makefile`, `test/host/fake_hw.{c,h}`, `test/host/test_battsave.c` | Host unit tests |
| `core/battsave_hw.c` | Camera binding of `battsave_hw_t` (per-camera THUMB build) |
| `tools/battsave/build_sx50.sh` | Docker build of SX50 100b + 100c |
| `.github/workflows/battsave.yml` | CI: host tests + both builds |
| Modified: `core/main.c`, `core/kbd_process.c`, `core/conf.c`, `include/conf.h`, `core/gui.c`, `core/gui_lang.h`, `CHDK/LANG/english.lng`, `include/camera.h`, `platform/sx50hs/platform_camera.h`, `lib/core/Makefile`, `platform/makefile_sub.inc` | Hooks, settings, menu, defines, build |

---

### Task 1: On-camera power probe (throwaway)

**Files:**
- Create: `test/battsave_probe.lua`, `test/analyze_probe.py`, `test/test_analyze_probe.py`, `test/README_probe.md`

**Interfaces:**
- Produces: CSV `A/battprobe.csv` with header `tick_ms,mode,block,vbatt_mv`; `mode` ∈ `rec_on`, `rec_off`, `play_off`.
- Produces: `analyze(rows) -> dict[mode, float mV_per_hour]` and `verdict(rates) -> str` in `test/analyze_probe.py`.

- [ ] **Step 1: Write failing analyzer tests** in `test/test_analyze_probe.py`:
  - `test_slope_per_mode`: synthetic rows where `rec_on` drops 60 mV/h, `rec_off` 40 mV/h, `play_off` 20 mV/h across interleaved 20-min blocks → `analyze()` returns each within ±1 mV/h.
  - `test_verdict_go`: rates `{rec_off: 40, play_off: 20}` → `verdict()` starts with `"GO"`.
  - `test_verdict_nogo`: rates `{rec_off: 40, play_off: 35}` → starts with `"NO-GO"` (threshold: play_off must be ≤ 75% of rec_off).
  - `test_skips_first_minute_of_block`: a 30 mV spike in each block's first 60 s does not change the slope by more than 1 mV/h.
- [ ] **Step 2: Run** `python3 -m pytest test/test_analyze_probe.py -v` — Expected: FAIL (module not found).
- [ ] **Step 3: Implement `test/analyze_probe.py`**: per block, drop samples in the first 60 s (mode-switch settling), least-squares slope of `vbatt_mv` vs hours; per mode, median of block slopes (reported as positive drain). `main()` reads a CSV path from argv and prints a table plus `verdict()`. Stdlib only.
- [ ] **Step 4: Run** the tests — Expected: 4 passed.
- [ ] **Step 5: Write `test/battsave_probe.lua`**: CHDK script header (`@title Battery probe`, `@chdk_version 1.4`); `@param b Block minutes` default 20; `@param h Total hours` default 6. Loop cycling modes `rec_on` → `rec_off` → `play_off`: use `set_record(true/false)` (wait until `get_mode()` confirms, max 10 s), `set_lcd_display(0/1)` and `set_backlight(0/1)` re-asserted every sample (Canon re-enables them). Sample every 30 s: append `get_tick_count(),mode,block,get_vbatt()` to `A/battprobe.csv`, open/close per write. At end: return to rec mode, display on.
- [ ] **Step 6: Write `test/README_probe.md`** (user instructions): full charge; Canon menu → Lens Retract = 0 sec; CHDK Misc → Disable LCD off = Always; load and run the script; leave ~6 h; copy `battprobe.csv` off the card; run `python3 test/analyze_probe.py battprobe.csv`.
- [ ] **Step 7: Syntax-check the Lua** with `luac -p test/battsave_probe.lua` if `luac` is available (`brew install lua`); Expected: no output.
- [ ] **Step 8: Commit** `test/battsave_probe.lua test/analyze_probe.py test/test_analyze_probe.py test/README_probe.md` — `"battsave: add on-camera power probe and analyzer"`.

> **Gate:** hand the probe to the user now. Tasks 2–5 proceed in parallel; Task 6 (camera integration of DEEP) waits for a GO verdict or a user decision.

---

### Task 2: State machine skeleton, host harness, AWAKE ↔ LIGHT

**Files:**
- Create: `include/battsave.h`, `core/battsave.c`, `test/host/Makefile`, `test/host/fake_hw.h`, `test/host/fake_hw.c`, `test/host/test_battsave.c`

**Interfaces (Produces — used by every later task):**

```c
typedef enum { BS_AWAKE, BS_LIGHT, BS_DEEP, BS_WAKING, BS_WAKE_FAILED, BS_POWER_OFF } battsave_state_t;
#define BS_NEVER 0u   /* timeout value meaning "never" */

typedef struct {
    int      enabled;
    unsigned t1_ms, t2_ms, t3_ms;   /* t2/t3 may be BS_NEVER */
    int      blink;                 /* 0 also when port has no LED */
    int      deep_allowed;          /* 0 when CAM_BATTSAVE_NO_DEEP */
    int      zoom_allowed;          /* 0 when !CAM_HAS_ZOOM_LEVER */
    int      low_batt_mv;           /* 0 = no check */
} battsave_config_t;

typedef struct {
    void (*display)(int on);
    void (*led)(int on);
    int  (*is_rec_mode)(void);
    void (*set_rec_mode)(int rec);
    int  (*get_zoom)(void);
    void (*set_zoom)(int z);
    void (*power_off)(void);
    int  (*vbatt_mv)(void);
    int  (*inhibit)(void);          /* script running || movie recording || USB connected */
} battsave_hw_t;

void             battsave_init(const battsave_hw_t *hw, const battsave_config_t *cfg, unsigned now_ms);
void             battsave_set_config(const battsave_config_t *cfg, unsigned now_ms);
void             battsave_tick(unsigned now_ms);
int              battsave_key(int any_key_down, unsigned now_ms); /* returns 1 = swallow keys from Canon */
battsave_state_t battsave_state(void);
int              battsave_overlays_paused(void);                  /* 1 in LIGHT, DEEP, WAKING */
int              battsave_enabled(void);                          /* current config's enabled flag */
```

`fake_hw.h` exposes a global `fake_hw_t fake` with fields `display_on, led_on, rec_mode, zoom, powered_off, vbatt, inhibit, set_rec_calls, set_zoom_last, rec_mode_follows` (when `rec_mode_follows` is 1, `set_rec_mode` immediately sets `rec_mode`), plus `fake_reset()` and `extern const battsave_hw_t fake_hw_ops`.

- [ ] **Step 1: Write the harness and failing tests.** `test/host/Makefile`: `cc -std=c99 -Wall -Wextra -Werror -I../../include -o test_battsave test_battsave.c fake_hw.c ../../core/battsave.c && ./test_battsave`. Minimal `CHECK(cond)` macro printing file:line, counting failures, nonzero exit on failure. Helper `cfg_default()` returns enabled=1, t1=30000, t2=180000, t3=7200000, blink=1, deep_allowed=1, zoom_allowed=1, low_batt_mv=0. Helper `run(from, to)` calls `battsave_tick` every 20 ms. Tests:
  - `test_disabled_never_sleeps`: enabled=0, run 0→10 h → state AWAKE, `display_on==1`.
  - `test_light_after_t1`: at 29 980 ms AWAKE; at 30 000 ms LIGHT, `display_on==0`, `battsave_overlays_paused()==1`.
  - `test_light_key_passes_and_wakes`: in LIGHT, `battsave_key(1, t)` returns 0, state AWAKE, `display_on==1`.
  - `test_activity_resets_idle`: key at 20 s → still AWAKE at 45 s, LIGHT at 50 s.
  - `test_inhibit_blocks_sleep`: `fake.inhibit=1` → AWAKE after 10 min; inhibit raised while LIGHT → AWAKE, display on.
  - `test_user_playback_blocks_sleep`: `fake.rec_mode=0` before T1 → AWAKE after 10 min.
  - `test_light_sleep_across_tick_wrap`: init at `now=0xFFFFFFFFu - 10000`, run 40 s → LIGHT at exactly 30 s of elapsed ticks.
- [ ] **Step 2: Run** `make -C test/host` — Expected: compile error (battsave.c missing).
- [ ] **Step 3: Implement** `include/battsave.h` as above and the AWAKE/LIGHT part of `core/battsave.c` (file-static state, `last_activity_ms`, `state_since_ms`). DEEP-related transitions are stubs for Task 3.
- [ ] **Step 4: Run** `make -C test/host` — Expected: all tests pass, exit 0.
- [ ] **Step 5: Commit** — `"battsave: state machine skeleton with light sleep and host tests"`.

---

### Task 3: DEEP sleep, LED blink, wake path

**Files:** Modify `core/battsave.c`; Test: `test/host/test_battsave.c`

**Interfaces:** Consumes Task 2 API. No new public symbols.

- [ ] **Step 1: Write failing tests:**
  - `test_deep_after_t2`: at 180 000 ms DEEP; `fake.rec_mode==0`; zoom saved (set `fake.zoom=42` before).
  - `test_t2_never_stays_light`: t2=BS_NEVER → LIGHT after 10 h.
  - `test_t2_le_t1_goes_straight_deep`: t1=60 000, t2=30 000 → AWAKE at 59 s, DEEP at 60 s (never LIGHT).
  - `test_deep_not_allowed`: deep_allowed=0 → LIGHT after 10 h.
  - `test_deep_blink_pattern`: over 3000 ms window in DEEP, `led_on` is 1 for 40–60 ms total and 0 otherwise; blink=0 → LED stays off in DEEP.
  - `test_deep_wake_restores`: in DEEP, `battsave_key(1,t)` returns 1, state WAKING, `set_rec_mode(1)` called; after rec confirms → AWAKE, `display_on==1`, `led_on==1`, `set_zoom_last==42`.
  - `test_deep_wake_swallows_until_release`: key held across 2 s of ticks after wake → every `battsave_key(1,…)` returns 1; `battsave_key(0,…)` then `battsave_key(1,…)` → returns 0.
  - `test_zoom_skip_if_moved`: during WAKING, `fake.zoom=10` before rec confirms → `set_zoom` not called.
  - `test_zoom_disallowed`: zoom_allowed=0 → `get_zoom`/`set_zoom` never called.
  - `test_wake_retry_then_fail`: `rec_mode_follows=0` → state WAKING until 5000 ms after wake, then WAKE_FAILED, `display_on==1`, LED toggles every 250 ms; next key → AWAKE (LED on, returns 0).
- [ ] **Step 2: Run** `make -C test/host` — Expected: new tests FAIL.
- [ ] **Step 3: Implement** DEEP entry (save zoom if allowed, `set_rec_mode(0)`), blink (`(now - state_since) % 3000 < 50`), WAKING (call `set_rec_mode(1)` once, poll `is_rec_mode()` each tick, 5000 ms deadline), zoom restore only when `get_zoom()==0` after rec confirms, WAKE_FAILED fast blink, and a `swallow_until_release` flag.
- [ ] **Step 4: Run** `make -C test/host` — Expected: all pass.
- [ ] **Step 5: Commit** — `"battsave: deep sleep with lens retract, blink, and wake handling"`.

---

### Task 4: Safety power-off, low battery, config mapping

**Files:** Modify `include/battsave.h`, `core/battsave.c`; Test: `test/host/test_battsave.c`

**Interfaces (Produces, used by Task 7):**

```c
/* Menu index → config. Index tables: T1 {10s,30s,1m,2m}; T2 {1m,3m,5m,10m,never}; T3 {30m,1h,2h,4h,never}. */
#define BS_T1_DEFAULT_IDX 1
#define BS_T2_DEFAULT_IDX 1
#define BS_T3_DEFAULT_IDX 2
void battsave_config_from_menu(battsave_config_t *out, int enabled, int t1_idx, int t2_idx, int t3_idx, int blink);
```
(Fills only the timing/enable/blink fields; out-of-range indices clamp to the default index.)

- [ ] **Step 1: Write failing tests:**
  - `test_power_off_after_t3`: DEEP entered at 180 s; at 180 s + 7 200 000 ms → `powered_off==1`, state POWER_OFF; 1 ms earlier → not.
  - `test_t3_never`: t3=BS_NEVER → not powered off after 48 h.
  - `test_low_battery_powers_off`: low_batt_mv=3300, in DEEP set `fake.vbatt=3299` → POWER_OFF on next tick; `low_batt_mv=0` → ignored.
  - `test_low_battery_ignored_when_awake`: AWAKE with low vbatt → no power off (Canon handles it).
  - `test_inhibit_in_deep_wakes`: `fake.inhibit=1` while DEEP → state WAKING.
  - `test_disable_while_asleep_restores`: in LIGHT and in DEEP, `battsave_set_config(enabled=0)` → display on, LED on, AWAKE (from DEEP: via WAKING).
  - `test_config_from_menu`: idx (1,1,2) → 30000, 180000, 7200000; t2 idx 4 → BS_NEVER; t3 idx 4 → BS_NEVER; t1 idx 9 → 30000.
- [ ] **Step 2: Run** `make -C test/host` — Expected: new tests FAIL.
- [ ] **Step 3: Implement** T3 and low-battery checks (DEEP only), inhibit-wakes-DEEP, disable handling in `battsave_set_config`, and `battsave_config_from_menu` with static `const unsigned` tables.
- [ ] **Step 4: Run** `make -C test/host` — Expected: all pass.
- [ ] **Step 5: Commit** — `"battsave: safety power-off, low-battery cutoff, menu config mapping"`.

---

### Task 5: Docker build of stock SX50

**Files:** Create `tools/battsave/build_sx50.sh`

**Interfaces:** Produces `tools/battsave/build_sx50.sh [100b|100c|all]` → zips in `bin/`.

- [ ] **Step 1: Write the script**: `docker build --platform linux/amd64 -t chdkbuild tools/docker/chdkbuild` (once), then for each sub `docker run --rm --platform linux/amd64 -v "$PWD":/srv/src chdkbuild make PLATFORM=sx50hs PLATFORMSUB=$sub firzipsub`. `set -euo pipefail`. If the Debian stretch apt step fails, patch the Dockerfile `FROM` line's sources to `archive.debian.org` inside the script's build context copy (do not edit the upstream Dockerfile).
- [ ] **Step 2: Run** `tools/battsave/build_sx50.sh all` on the unmodified tree — Expected: exits 0; `ls bin/*sx50hs*` shows a 100b and a 100c zip.
- [ ] **Step 3: Commit** — `"battsave: docker build script for SX50 HS"`.

---

### Task 6: Camera integration (hooks + hardware binding)

**Files:**
- Create: `core/battsave_hw.c`
- Modify: `include/camera.h` (defaults), `platform/sx50hs/platform_camera.h`, `lib/core/Makefile` (add `$Obattsave.o`), `platform/makefile_sub.inc` (add `battsave_hw.thm.o`), `core/main.c` (spytask loop ~line 300–380), `core/kbd_process.c:81`, `core/conf.c:56`

**Interfaces:**
- Consumes: Task 2–4 API.
- Produces: `void battsave_hw_init(void);` and `void battsave_hw_apply_conf(void);` (declared in `include/battsave.h` under `#ifdef CAM_HAS_BATTSAVE`). `apply_conf` reads conf fields from Task 7; until then it uses compile-time defaults with `enabled=1` **only in a local test build** — commit with `enabled=0`.

- [ ] **Step 1: Defines.** `include/camera.h`: `#undef CAM_HAS_BATTSAVE`, `#undef CAM_BATTSAVE_LED`, `#undef CAM_BATTSAVE_NO_DEEP` in the defaults block. SX50 `platform_camera.h`: `#define CAM_HAS_BATTSAVE 1`, `#define CAM_BATTSAVE_LED 0`.
- [ ] **Step 2: `core/battsave_hw.c`** (whole file inside `#ifdef CAM_HAS_BATTSAVE`): `display(on)` → `TurnOnDisplay(); TurnOnBackLight();` / `TurnOffBackLight(); TurnOffDisplay();`; `led(on)` → `camera_set_led(CAM_BATTSAVE_LED, on, 0)`; `is_rec_mode` → `camera_info.state.mode_rec`; `set_rec_mode` → `shooting_set_playrec_mode(rec)`; zoom → `shooting_get_zoom`/`shooting_set_zoom`; `power_off` → `shutdown()`; `vbatt_mv` → `stat_get_vbatt()`; `inhibit` → `camera_info.state.state_kbd_script_run || is_video_recording() || get_usb_bit()`. Config: `blink` forced 0 without `CAM_BATTSAVE_LED`, `deep_allowed` 0 with `CAM_BATTSAVE_NO_DEEP`, `zoom_allowed` = `CAM_HAS_ZOOM_LEVER`, `low_batt_mv = conf.batt_volts_min`.
- [ ] **Step 3: Hooks** (each under `#ifdef CAM_HAS_BATTSAVE`):
  - `core/main.c`: `battsave_hw_init()` before the loop; `battsave_tick(get_tick_count())` each iteration; wrap the `gui_redraw()` block and the histogram/edge-overlay block in `if (!battsave_overlays_paused())`.
  - `core/kbd_process.c` at the top of `kbd_process()`: `if (battsave_key(kbd_get_pressed_key() != 0, get_tick_count())) return 1;` — confirm in `kbd_update_key_state()` (`core/kbd_common.c:203`) that a nonzero return hides keys from Canon (uses `kbd_mod_state`, which must read as all-released when no CHDK key emulation is active); if it does not, add a cleared-mask path and note it in the commit.
  - `core/conf.c` `conf_update_prevent_shutdown()`: add `|| conf.battsave_enable` to the disable-shutdown condition (field from Task 7; in this task use a `battsave_enabled()` query returning the current config's `enabled`).
- [ ] **Step 4: Build** `tools/battsave/build_sx50.sh all` — Expected: both zips build, no new warnings in battsave files.
- [ ] **Step 5: Host tests still pass**: `make -C test/host` — Expected: all pass.
- [ ] **Step 6: Commit** — `"battsave: integrate state machine into SX50 build"`.
- [ ] **Step 7 (user, on camera, Review Focus #4):** local build with `enabled=1` and T1=10 s, T2=1 min, Lens Retract=0 sec. Verify: screen off at 10 s; half-press wakes instantly and a full press shoots; at 1 min lens retracts and green LED blinks; half-press → lens extends, zoom restored. If the play/rec switch misbehaves, apply the pending-request fallback from Review Focus #4 and repeat.

---

### Task 7: Menu, persisted settings, language strings

**Files:** Modify `include/conf.h`, `core/conf.c` (CONF_INFO table near line 521), `core/gui.c` (Misc menu near line 2179), `core/gui_lang.h`, `CHDK/LANG/english.lng`, `core/battsave_hw.c`

**Interfaces:**
- Consumes: `battsave_config_from_menu`, `BS_*_DEFAULT_IDX` (Task 4); `battsave_hw_apply_conf()` (Task 6).
- Produces: conf fields `battsave_enable, battsave_t1, battsave_t2, battsave_t3, battsave_blink` (ints, menu indices).

- [ ] **Step 1: Conf.** Add the five ints to `conf` in `include/conf.h`. Add `CONF_INFO(299…303)` with defaults 0, `BS_T1_DEFAULT_IDX`, `BS_T2_DEFAULT_IDX`, `BS_T3_DEFAULT_IDX`, 1, each with a change callback that calls `battsave_hw_apply_conf()` (and `conf_update_prevent_shutdown()` for enable). Guard with `#ifdef CAM_HAS_BATTSAVE`.
- [ ] **Step 2: Strings.** `english.lng` entries 688–693: `"Battery Saver"` (submenu title + link), `"Enable"`, `"Screen off after"`, `"Deep sleep after"`, `"Power off after"`, `"Sleep blink"`; matching `LANG_MENU_BATTSAVE_*` defines in `core/gui_lang.h`; bump `GUI_LANG_ITEMS`. Other languages fall back to English.
- [ ] **Step 3: Menu.** Submenu `battsave_submenu` with `MENU_ITEM … MENUITEM_BOOL` for enable/blink and `MENU_ENUM2` for T1/T2/T3 using string arrays `{"10s","30s","1m","2m"}`, `{"1m","3m","5m","10m","Never"}`, `{"30m","1h","2h","4h","Never"}`, plus `MENU_ITEM(0x51, LANG_MENU_BACK, MENUITEM_UP…)`. Link it from the Misc menu directly after "Disable LCD off" (`core/gui.c:2179`). Hide blink item when `CAM_BATTSAVE_LED` is undefined.
- [ ] **Step 4: `battsave_hw_apply_conf()`** now builds config from `conf.battsave_*` via `battsave_config_from_menu` and calls `battsave_set_config(&cfg, get_tick_count())`; remove the Task 6 hardcoded config.
- [ ] **Step 5: Build** both firmwares and run host tests — Expected: both pass; `bin/` zips present.
- [ ] **Step 6: Commit** — `"battsave: menu, persisted settings, and strings"`.

---

### Task 8: GitHub Actions

**Files:** Create `.github/workflows/battsave.yml`

- [ ] **Step 1: Workflow** on push/PR to `battery-saver` and `trunk`: job `host-tests` (ubuntu-latest: `make -C test/host`; `pip install pytest && python3 -m pytest test/test_analyze_probe.py`); job `build-sx50` (ubuntu-latest, matrix sub ∈ {100b, 100c}: `docker build -t chdkbuild tools/docker/chdkbuild`, run `make PLATFORM=sx50hs PLATFORMSUB=${{ matrix.sub }} firzipsub`, `actions/upload-artifact` of `bin/*.zip`). Reuse Task 5's archive.debian.org workaround if needed (call `tools/battsave/build_sx50.sh ${{ matrix.sub }}`).
- [ ] **Step 2: Push** the branch; `gh run watch` — Expected: all jobs green, two artifacts downloadable.
- [ ] **Step 3: Commit** (before push) — `"ci: host tests and SX50 builds for battery saver"`.

---

### Task 9: Final on-camera check (user)

**Files:** Create `test/README_final_check.md`

> Spec deviation: the spec's step 4 says "same voltage logging", but a running Lua script inhibits sleep by design, so the probe script cannot measure the real feature. The final check measures standby endurance instead (still on / powered off by T3 / battery bars). If numeric logging is wanted, add a C-side `A/BATTSAVE.LOG` writer as a follow-up task.

- [ ] **Step 1: Write instructions:** install the CI artifact for the camera's firmware; Lens Retract = 0 sec; enable Battery Saver with defaults; full charge; leave idle overnight; next morning note whether the camera is still on (LED blinking) or powered off by T3, and battery bars. Also run the four-point manual check from Task 6 Step 7 with default timings.
- [ ] **Step 2: Commit** — `"battsave: final on-camera check instructions"`.
- [ ] **Step 3:** User reports results; compare against the probe's `play_off` rate. Record outcome in the spec's Delivery section.
