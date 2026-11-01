# CHDK with Battery Saver

A fork of [CHDK](https://chdk.fandom.com/wiki/CHDK) (Canon Hack Development Kit) that adds **Battery Saver**: a tiered sleep mode that cuts idle battery drain on Canon PowerShot cameras while staying ready to shoot.

Everything else is upstream CHDK 1.7 (from the [eoschdk/trunk](https://github.com/eoschdk/trunk) git mirror of the official SVN, r6346). The original readme is in [`README`](README); licence GPL-2.0 ([`COPYING`](COPYING)).

> **Status: experimental.** Built and unit-tested, not yet confirmed on a real camera. Only the **PowerShot SX50 HS** (firmware 1.00b and 1.00c) has it enabled.

## What it does

When the camera sits idle in shooting mode, Battery Saver steps down:

| After (default) | State | What happens | How to wake |
|---|---|---|---|
| 30 s | **Screen off** | Display and backlight off, CHDK overlays paused. Lens stays out. | Any key or dial. Keys pass through, so a full shutter press shoots right away. |
| 3 min | **Deep sleep** | Switches to playback so the sensor is off and the lens retracts. Zoom position saved. **The green power LED blinks briefly every 3 s**, so you can tell the camera is asleep, not off. | Any key or dial. The lens comes back out at the saved zoom. The waking press is not passed to the camera, so it never fires a shot by surprise. |
| 2 h in deep sleep | **Power off** | Normal shutdown. | Power button. |

It also powers off early in deep sleep if the battery stays below CHDK's "min voltage" setting for 30 s.

It never sleeps while a script runs, a video is recording, USB is connected, the CHDK menu is open, or you are in playback yourself. Canon's own auto power-off still works whenever Battery Saver can't power the camera off itself.

If the lens fails to come back out within 5 s, the screen turns on and the LED blinks fast. Press any key to dismiss.

## Settings

`<ALT>` → menu → **CHDK Settings → Battery Saver**. Changes apply when you leave `<ALT>` mode.

| Setting | Choices | Default |
|---|---|---|
| Enable | on / off | **off** |
| Screen off after | 10 s, 30 s, 1 min, 2 min | 30 s |
| Deep sleep after | 1, 3, 5, 10 min, never | 3 min |
| Power off after | 30 min, 1 h, 2 h, 4 h, never | 2 h |
| Sleep blink | on / off | on |
| Log to card | on / off: writes `A/BATTSAVE.LOG` | off |

Tip: set Canon menu → **Lens Retract → 0 sec** so the lens goes in as soon as deep sleep starts.

## Install (SX50 HS)

1. Find your firmware version (CHDK "Show build info", or `ver.req`): 1.00b or 1.00c.
2. Download `chdk-sx50hs-100b` or `chdk-sx50hs-100c` from the latest green run under [Actions](../../actions).
3. Unzip onto a CHDK-prepared SD card, replacing the existing files, and boot CHDK as usual.

Then work through [`test/README_final_check.md`](test/README_final_check.md). It's a 10-minute hands-on check plus one overnight run, and reports from it are what move this from experimental to tested.

## How it's built

| Piece | File | Notes |
|---|---|---|
| State machine | [`core/battsave.c`](core/battsave.c) | Pure logic: time is passed in, and hardware is reached only through a table of function pointers (`battsave_hw_t`). No libc, no camera calls. |
| Camera binding | [`core/battsave_hw.c`](core/battsave_hw.c) | Maps those hooks to CHDK APIs (display, LED, play/rec, zoom, battery, dial). Compiled only where `CAM_HAS_BATTSAVE` is set. |
| Hooks into CHDK | `core/main.c`, `core/kbd_process.c`, `core/conf.c`, `core/gui.c` | Tick every 20 ms in the spytask, key handling in the keyboard task, settings, menu. |
| Design | [`docs/superpowers/specs/`](docs/superpowers/specs/2026-11-01-battery-saver-design.md) | Behaviour, error handling, portability. |

### Tests (no camera needed)

```sh
make -C test/host                       # C unit tests of the state machine (host compiler)
python3 -m pytest test/ -q              # log and probe analysis tools
```

CI ([`.github/workflows/battsave.yml`](.github/workflows/battsave.yml)) runs both, cross-compiles the SX50 HS firmware for 1.00b and 1.00c, and builds three ports without Battery Saver (A560, SX40 HS, G7 X), checking their binaries contain none of its code.

To build locally with Docker: `tools/battsave/build_sx50.sh all` (or `tools/battsave/build.sh <platform> <sub>`).

## Adding another camera

The core is camera independent. To enable a port, add to its `platform_camera.h`:

```c
#define CAM_HAS_BATTSAVE        1
#define CAM_BATTSAVE_LED        0                   // camera_set_led index of a small status LED (omit: no blink; never the AF lamp)
#define CAM_BATTSAVE_DIAL_POS   rear_dial_position  // optional: raw dial counter from stubs, so dial turns count as activity
// #define CAM_BATTSAVE_NO_DEEP 1                   // if play/rec switching is unreliable: screen-off only
```

Then run the on-camera check for that model. Please report results, good or bad.

## Measuring

- [`test/battsave_probe.lua`](test/battsave_probe.lua) with [`test/analyze_probe.py`](test/analyze_probe.py): measures drain in shooting mode with the screen on, shooting mode with the screen off, and playback with the screen off, on stock CHDK. See [`test/README_probe.md`](test/README_probe.md).
- `A/BATTSAVE.LOG` with [`test/analyze_battsave_log.py`](test/analyze_battsave_log.py): time and drain per state from real use.

## Known limits

- On the SX50 HS the Power and Playback buttons can't be hidden from Canon, so pressing them during deep sleep also acts on the camera. Use the shutter or a dial to wake.- Keys pressed during the roughly 1 s wake-up reach the camera normally.
- Zoom is restored only if the lens is back at wide when the wake finishes, so it never fights a zoom you started yourself.
