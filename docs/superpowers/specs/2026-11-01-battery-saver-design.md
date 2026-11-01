# Battery Saver — Design

Date: 2026-11-01
Branch: `battery-saver`
First target: Canon PowerShot SX50 HS (firmware 1.00b, 1.00c)

## 1. Goal

Add a CHDK "Battery Saver" mode that greatly reduces battery drain for two uses:

- **Long standby** — camera stays on and ready for a long time but rarely shoots.
- **More shots per charge** — normal handheld shooting with less idle drain.

Success criteria:

1. Measured idle drain in deep sleep is substantially lower than stock idle (screen on, rec mode), confirmed by on-camera voltage logs.
2. Light-sleep wake is instant; a full shutter press from light sleep takes a picture.
3. People can tell a sleeping camera from a switched-off one (LED blink).
4. A forgotten camera still powers itself off (safety timer, low-battery cutoff).
5. Disabled by default; with it off, behavior is identical to stock CHDK.

Non-goals: changing Canon firmware power behavior beyond what existing CHDK APIs expose; video-mode power saving; interval/timelapse scheduling.

## 2. Behavior

### States

| State | Entered when | Screen | Lens / mode | Power LED | Wake |
|---|---|---|---|---|---|
| AWAKE | default / any key | on | as user left it | solid | — |
| LIGHT | no key for T1 | off; CHDK overlays paused | unchanged | solid | instant |
| DEEP | no key for T2 (total idle) | off | playback, lens retracted, zoom saved | short blink every 3 s | ~2–3 s |
| POWER_OFF | T3 elapsed in DEEP, or battery below `conf.batt_volts_min` | — | camera shuts down | off | power button |

### Rules

- **LIGHT wake:** keys pass through to Canon firmware. Full shutter press shoots immediately.
- **DEEP wake:** the waking key press is swallowed. Switch to rec mode, restore saved zoom, display on.
- **Never enter sleep while:** a script is running, video is recording, a USB/PTP session is active, or the user is already in playback mode by their own choice.
- **Canon Auto Power Down** is blocked while Battery Saver is enabled (reusing the `alt_prevent_shutdown` mechanism, `core/conf.c:56`). T3 replaces it.
- In LIGHT and DEEP, `core/main.c` skips `gui_redraw()`, histogram processing, and edge overlay.

## 3. Architecture

```
 core/main.c (20 ms loop) ── battsave_tick(now_ms) ──┐
 core/kbd_common.c (key scan) ── battsave_activity() ┤
                                                      ▼
                         core/battsave.c — pure state machine, no camera calls
                                                      │ battsave_hw_t (function pointers)
                         ┌────────────────────────────┴──────────────────┐
                 core/battsave_hw.c (camera)                  test/host/fake_hw.c (Mac)
```

### Units

- **`core/battsave.c` / `include/battsave.h`** — state machine and timers. Inputs: `battsave_tick(now_ms)`, `battsave_activity()`, `battsave_set_config(const battsave_config_t*)`, `battsave_set_hw(const battsave_hw_t*)`. Query: `battsave_state()`, `battsave_overlays_paused()`, `battsave_swallow_key()`. Time is always passed in; the module never reads a clock.
- **`battsave_hw_t`** — function pointers: `display_on/off`, `led_set(state)`, `is_rec_mode`, `set_rec_mode(bool)`, `get_zoom`, `set_zoom`, `prevent_shutdown(bool)`, `shutdown`, `get_vbatt_mv`, and inhibit queries `script_running`, `movie_recording`, `usb_connected`.
- **`core/battsave_hw.c`** — camera binding of `battsave_hw_t` to existing CHDK functions (`TurnOffDisplay`, `TurnOnDisplay`, `camera_set_led`, `shooting_set_playrec_mode`, `shooting_get_zoom`/`shooting_set_zoom`, `shutdown`, `get_vbatt`, `camera_info.state.*`).
- **`core/main.c`** — call `battsave_tick()` each loop; gate `gui_redraw`/histogram/edge overlay on `!battsave_overlays_paused()`.
- **`core/kbd_common.c`** — call `battsave_activity()` on any key change; drop the key when `battsave_swallow_key()` is true.
- **`core/gui.c`, `core/conf.c`, `include/conf.h`, lang files** — menu and persisted settings.

## 4. Settings

New submenu "Battery Saver" under Miscellaneous, near "Disable LCD off". Persisted as conf IDs 299–303.

| Setting | Values | Default |
|---|---|---|
| Battery Saver | on / off | off |
| Screen off after (T1) | 10 s, 30 s, 1 min, 2 min | 30 s |
| Deep sleep after (T2) | 1, 3, 5, 10 min, never | 3 min |
| Power off after (T3) | 30 min, 1 h, 2 h, 4 h, never | 2 h |
| Sleep blink | on / off | on |

## 5. Error handling

- **T2 ≤ T1:** treated as T2 = T1 (LIGHT is skipped, go straight to DEEP).
- **Rec-mode switch fails on wake:** retry for up to 5 s; if still not in rec mode, give up — display on, remain in playback, fast LED blink to signal a problem. Never loop indefinitely.
- **Zoom restore:** skipped if the zoom changed during wake.
- **Low battery in sleep:** `get_vbatt() < conf.batt_volts_min` → POWER_OFF.
- **CHDK not loaded:** stock camera, nothing changes.

## 6. Portability

The feature lives in platform-independent `core/` and uses only existing per-port CHDK APIs. Camera differences are expressed as `platform_camera.h` defines:

| Define | Meaning | Default |
|---|---|---|
| `CAM_HAS_BATTSAVE` | Feature compiled in for this port | undefined (off) — set only for tested ports; SX50 HS first |
| `CAM_BATTSAVE_LED` | `camera_set_led` index of a low-power status LED | undefined → sleep blink disabled (never blinks the AF lamp) |
| `CAM_BATTSAVE_NO_DEEP` | Port can't reliably switch play/rec; disable DEEP | undefined |

Zoom save/restore is skipped on ports without `CAM_HAS_ZOOM_LEVER`. Low-battery threshold is per-port already via `get_vbatt_min()`.

SX50 HS: `CAM_HAS_BATTSAVE 1`, `CAM_BATTSAVE_LED 0` (green power LED, `platform/sx50hs/lib.c:49`).

## 7. Testing

0. **On-camera probe (throwaway, before C work).** `test/battsave_probe.lua` cycles 20-minute blocks of (rec + screen on), (rec + screen off), (playback + screen off) for ~6 h, logging `get_vbatt()` every 30 s to `A/battprobe.csv`. Blocks are interleaved so each mode samples the same parts of the Li-ion discharge curve. `test/analyze_probe.py` reports mV/hour per mode. **Go/no-go:** if playback is not clearly better than rec + screen off, revisit DEEP before implementing.
1. **Host unit tests (TDD).** `test/host/test_battsave.c` + `fake_hw.c`, built with the Mac's native compiler via `make -C test/host`. Covers: every state transition and its timing; LIGHT vs DEEP wake key handling; each inhibit condition; T2 ≤ T1 and "never" values; rec-mode retry and give-up; low-battery cutoff; zoom-restore skip.
2. **Cross-compile.** Docker image with the CHDK ARM toolchain; `make PLATFORM=sx50hs PLATFORMSUB=100b fir` and `100c`.
3. **CI.** GitHub Actions on the fork: host tests + both SX50 builds per push; builds uploaded as artifacts.
4. **Final on-camera check.** Real build with the same voltage logging, compared against the step-0 baseline.

## 8. Delivery

Work on the `battery-saver` branch of `yudataguy/chdk`. If results are good, propose upstream on the CHDK forum.
