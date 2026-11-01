--[[
@title Battery probe
@chdk_version 1.4
#ui_block_min=20 "Block minutes" [5 60]
#ui_total_h=6 "Total hours" [1 24]

Throwaway power probe for the Battery Saver design.
Cycles rec+screen on / rec+screen off / playback+screen off in equal blocks,
logging battery voltage every 30 s to A/battprobe.csv.
Analyze with: python3 test/analyze_probe.py battprobe.csv
See test/README_probe.md for setup (Lens Retract = 0 sec, Disable LCD off = Always).
]]

local LOG = "A/battprobe.csv"
local SAMPLE_MS = 30000
local MODES = { "rec_on", "rec_off", "play_off" }

local block_ms = ui_block_min * 60 * 1000
local total_ms = ui_total_h * 60 * 60 * 1000

local function log_line(s)
  local f = io.open(LOG, "a")
  if f then
    f:write(s, "\n")
    f:close()
  end
end

-- switch play/rec and wait (max 10 s) until the camera confirms
local function want_rec(rec)
  local is_rec = get_mode()
  if is_rec == rec then return true end
  set_record(rec)
  local deadline = get_tick_count() + 10000
  while get_tick_count() < deadline do
    sleep(100)
    if get_mode() == rec then return true end
  end
  return false
end

-- Canon firmware re-enables the display on its own, so re-assert every sample
local function apply_display(mode)
  local on = (mode == "rec_on") and 1 or 0
  set_lcd_display(on)
  set_backlight(on)
end

local function apply_mode(mode)
  if not want_rec(mode ~= "play_off") then
    log_line("# warning: mode switch timed out for " .. mode)
  end
  apply_display(mode)
end

-- header only for a new file
do
  local f = io.open(LOG, "r")
  if f then f:close() else log_line("tick_ms,mode,block,vbatt_mv") end
end
log_line("# start block_min=" .. ui_block_min .. " total_h=" .. ui_total_h)

local t0 = get_tick_count()
local block = 0
while get_tick_count() - t0 < total_ms do
  local mode = MODES[(block % #MODES) + 1]
  apply_mode(mode)
  local block_start = get_tick_count()
  while get_tick_count() - block_start < block_ms and get_tick_count() - t0 < total_ms do
    apply_display(mode)
    log_line(string.format("%d,%s,%d,%d", get_tick_count(), mode, block, get_vbatt()))
    sleep(SAMPLE_MS)
  end
  block = block + 1
end

want_rec(true)
set_lcd_display(1)
set_backlight(1)
log_line("# done")
