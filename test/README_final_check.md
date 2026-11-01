# Battery Saver: on-camera check (SX50 HS)

Two short sessions: a hands-on check (about 10 minutes), then one overnight run.

## Install

1. On your camera: Canon menu → firmware version (shown in the CHDK "Show build info" screen, or via `ver.req`). It's either **1.00b** or **1.00c**.
2. Download the matching build from the latest green run at
   https://github.com/yudataguy/chdk/actions (artifact `chdk-sx50hs-100b` or `chdk-sx50hs-100c`).
3. Unzip it onto the SD card, overwriting the existing CHDK files, then boot CHDK as usual.
4. Canon menu → **Lens Retract → 0 sec**.

## Part 1: hands-on (about 10 minutes)

CHDK `<ALT>` → menu → **CHDK Settings → Battery Saver**:
- Enable: on
- Screen off after: **10s**
- Deep sleep after: **1m**
- Power off after: 30m

Leave `<ALT>` mode (settings apply when you leave). Then:

| # | Do | Expect |
|---|---|---|
| 1 | Wait 10 s without touching anything | Screen goes dark, lens stays out |
| 2 | Half-press the shutter | Screen back instantly |
| 3 | Wait 10 s, then press the shutter fully | Photo taken right away |
| 4 | Zoom in partway, wait 1 min | Lens retracts; green LED near the power button blinks every 3 s |
| 5 | Half-press the shutter | Lens comes out, zoom returns to where it was, screen on; no photo taken |
| 6 | While in deep sleep, press and **hold** half-press | Wakes up, and nothing reacts to the held press until you let go |
| 7 | Plug in the USB cable, wait 1 min | Never sleeps while USB is connected |

If step 4 or 5 misbehaves (for example the lens doesn't move, the camera freezes, or the LED blinks fast and the screen stays in playback), write down exactly what happened. That's the one part we couldn't test without the camera.

## Part 2: overnight

1. Fully charge the battery.
2. Battery Saver settings: Screen off after **30s**, Deep sleep after **3m**, Power off after **Never**.
3. Leave the camera on a table and go to bed. Note the time.
4. In the morning, record:
   - Is the green LED still blinking? (The camera is still alive.)
   - If it has died, roughly when? (Any photo timestamp or log helps.)
   - The battery level shown after waking.

For comparison, do the same with Battery Saver **off** and CHDK "Disable LCD off" set to Always (stock idle with the screen on). Stop that run once the battery warning appears, and note how long it took.

Send me both results, together with the output from `test/analyze_probe.py` if you ran the probe.
