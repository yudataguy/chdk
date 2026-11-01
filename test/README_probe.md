# Battery probe (one overnight run)

This answers one question before we build deep sleep: **does playback mode (lens
retracted, sensor off) drain the SX50 much less than shooting mode with only the
screen off?**

## Setup

1. Fully charge the battery.
2. Copy `test/battsave_probe.lua` to `A/CHDK/SCRIPTS/` on the SD card.
3. Boot CHDK on the camera.
4. **Canon menu → Lens Retract → 0 sec**, so the lens retracts right away in playback.
5. **CHDK menu → Miscellaneous → Disable LCD off → Always**, so Canon doesn't auto-power-off.
6. Put the camera on a table, with nothing pressing the buttons.

## Run

1. CHDK `<ALT>` → Script → Load script → `battsave_probe.lua`.
2. Keep the defaults (20-minute blocks, 6 hours) and press the shutter to start.
3. Leave it alone. The screen turns on and off and the lens goes in and out every 20 minutes; this is expected.

## Afterwards

1. Copy `A/battprobe.csv` from the card to your Mac.
2. Run:
   ```
   python3 test/analyze_probe.py battprobe.csv
   ```
3. Send me the output. A `GO` line means the deep-sleep design is worth it as planned.

Write down whether you changed Lens Retract. If the battery dies before 6 hours, that's fine; the CSV is still usable.
