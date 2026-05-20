-- ConnexInsights RF Profile Manager
-- Sub-GHz dual-LR1121 zone control and transition monitor
-- Load from EdgeTX: SCRIPTS/TOOLS/connexinsights_rf.lua
--
-- Protocol:
--   STATUS (read-only) — received via CRSF info extended frame (0x2E) carrying
--     tag 0xC0 + 14-byte payload from devRFProfile.cpp / ConnexInsights_SerializeTelemetry().
--     Payload layout (bytes after tag):
--       [0]  active_path   (0=A, 1=B)
--       [1]  candidate_path (0xFF=none)
--       [2]  mbb_state
--       [3]  LQ path A
--       [4]  RSSI path A (bias +200)
--       [5]  SNR path A (signed as uint8)
--       [6]  LQ path B
--       [7]  RSSI path B
--       [8]  SNR path B
--       [9]  audit_status (0=OK 1=WARN 2=FAIL)
--       [10-13] config_hash big-endian
--
--   CONFIG (writable) — standard CRSF parameter read/write to registered params:
--     The "RF Profile" folder parameters are discovered via the normal parameter list.
--     The script looks for params named "Primary Zone", "Secondary Zone",
--     "Trans Policy", "Guard Band MHz" in the parameter list.

local deviceId  = 0xEE  -- ELRS TX module
local handsetId = 0xEF

-- ── Parameter discovery ─────────────────────────────────────────────────────
-- Map firmware parameter names → discovered chunk indices.
-- Populated by 0x2B request / 0x2C response at init.
local PNAME_TO_KEY = {
  ["Primary Zone"]   = "ZONE_PRIMARY",
  ["Secondary Zone"] = "ZONE_SECONDARY",
  ["Trans Policy"]   = "TRANS_POLICY",
  ["Guard Band MHz"] = "GUARD_BAND",
}
local DISC_MAX = 32   -- stop after this many param entries
local s_pidx = {}     -- key → chunk index
local s_disc_chunk  = 0
local s_disc_done   = false
local s_disc_last_t = 0

local function push(key, value)
  local idx = s_pidx[key]
  if idx then
    crossfireTelemetryPush(0x2D, {deviceId, handsetId, idx, value})
  end
end

-- ── Display layout ──────────────────────────────────────────────────────────
local LCD_W, LCD_H = LCD_W, LCD_H
local COL1, COL2 = 2, LCD_W / 2

-- Zone options
local ZONE_OPTS = { [0]="LOW", [1]="MID", [2]="HIGH" }
-- Transition policy options
local POLICY_OPTS = { [0]="THRESHOLD", [1]="COMMANDED", [2]="PERIODIC" }
-- MBB state names
local MBB_STATES = {
  [0]="STABLE",     [1]="PREPARE",     [2]="SCANNING",
  [3]="CAND_LOCK",  [4]="DUAL_LOCK",   [5]="COMMITTING",
  [6]="VERIFY_REL", [7]="ROLLBACK",    [8]="FAILSAFE"
}
-- Path names
local PATH_NAMES = { [0]="A", [1]="B", [255]="NONE" }

-- ── State ────────────────────────────────────────────────────────────────────
local s = {
  zone_primary    = 0,   zone_secondary   = 1,
  trans_policy    = 0,   guard_band       = 4,
  active_path     = 0,   candidate_path   = 255,
  mbb_state       = 0,
  lq_a = 0,  rssi_a = -120,  snr_a = 0,
  lq_b = 0,  rssi_b = -120,  snr_b = 0,
  config_hash     = 0,  audit_status     = 0,
  dtv_version     = 0,
  -- editing
  cursor          = 1,   editing          = false,
  page            = 1,   -- 1=Config 2=Status 3=Transition
  -- pending transition
  pending_zone    = nil,
  trans_state     = "IDLE",  -- IDLE / VALIDATING / CONFIRMED / EXECUTING / DONE
  last_tick       = 0,
  poll_interval   = 500, -- ms between polls
}

-- ── Telemetry helpers ─────────────────────────────────────────────────────────
-- Status is received passively via crossfireTelemetryPop() — no polling needed.
-- Config writes go via the standard ELRS Lua parameter write (CRSF 0x2D).
local function send_transition_cmd(zone_target)
  -- zone_target: 0=ABORT, 1=LOW, 2=MID, 3=HIGH  (matches rfzone_name_e + 1)
  crossfireTelemetryPush(0x2D, { deviceId, handsetId, 0x7B, zone_target })
end

-- CX_TELEMETRY_TAG for the RF status frame
local CX_RF_STATUS_TAG = 0xC0

-- Parse the custom RF status telemetry frame from devRFProfile.cpp
-- Frame type 0x2E (ELRS info) carrying CX_RF_STATUS_TAG + 14 bytes
local function parse_rf_status_frame(data)
  -- data[1] is the tag, data[2..15] is the payload
  if #data < 15 then return end
  s.active_path    = data[2]
  s.candidate_path = data[3]
  local new_mbb    = data[4]
  s.lq_a           = data[5]
  s.rssi_a         = data[6] - 200          -- debias
  s.snr_a          = data[7] > 127 and data[7] - 256 or data[7]  -- signed
  s.lq_b           = data[8]
  s.rssi_b         = data[9] - 200
  s.snr_b          = data[10] > 127 and data[10] - 256 or data[10]
  s.audit_status   = data[11]
  s.config_hash    = data[12]*0x1000000 + data[13]*0x10000 + data[14]*0x100 + data[15]

  -- Track MBB state transitions for the transition UI
  if s.trans_state == "EXECUTING" then
    if new_mbb == 0 then s.trans_state = "DONE"
    elseif new_mbb == 7 then s.trans_state = "ROLLBACK"
    elseif new_mbb == 8 then s.trans_state = "FAILSAFE"
    end
  end
  s.mbb_state = new_mbb
end

-- crossfireTelemetryPop() returns (frame_type, data_table) where
-- frame_type is the CRSF type byte and data_table begins with the
-- first payload byte (after the extended header dest/orig addresses).
-- For our status frame: frame_type=0x2E, data[1]=0xC0 (tag), data[2..]=payload.
local function parse_response(frame_type, data)
  if not data or #data < 2 then return end
  if frame_type == 0x2E and data[1] == CX_RF_STATUS_TAG then
    parse_rf_status_frame(data)
  elseif frame_type == 0x2C and not s_disc_done and #data >= 4 then
    -- Parameter info response: data[1]=param_num, data[2]=parent, data[3]=type, data[4..]=name
    local chunk_idx = data[1]
    local name = ""
    for i = 4, #data do
      if data[i] == 0 then break end
      name = name .. string.char(data[i])
    end
    local key = PNAME_TO_KEY[name]
    if key then s_pidx[key] = chunk_idx end
    s_disc_chunk = chunk_idx + 1
    local found = 0
    for _, v in pairs(s_pidx) do if v then found = found + 1 end end
    if found == 4 or s_disc_chunk >= DISC_MAX then s_disc_done = true end
  end
end

-- ── Drawing ───────────────────────────────────────────────────────────────────
local function row(y, label, value, selected, editing_now)
  local attr = selected and (editing_now and BLINK+INVERS or INVERS) or 0
  lcd.drawText(COL1, y, label, 0)
  lcd.drawText(COL2, y, value, attr)
end

local function draw_config(cursor, edit)
  local y = 0
  lcd.drawText(0, y, "RF Config", BOLD); y = y + 9
  row(y, "Primary Zone",  ZONE_OPTS[s.zone_primary]   or "?", cursor==1, edit); y=y+9
  row(y, "Secondary Zone",ZONE_OPTS[s.zone_secondary]  or "?", cursor==2, edit); y=y+9
  row(y, "Trans Policy",  POLICY_OPTS[s.trans_policy]  or "?", cursor==3, edit); y=y+9
  row(y, "Guard Band MHz",tostring(s.guard_band),           cursor==4, edit); y=y+9
  row(y, "Config Hash",   string.format("0x%08X", s.config_hash), false, false); y=y+9
  row(y, "DTV Excl Ver",  string.format("0x%08X", s.dtv_version), false, false); y=y+9
  lcd.drawText(COL1, LCD_H-9, "[MENU]=Back  [ENT]=Edit  [R/L]=Nav", SMLSIZE)
end

local function draw_status(cursor)
  local y = 0
  lcd.drawText(0, y, "RF Status", BOLD); y = y + 9
  local mbb_name = MBB_STATES[s.mbb_state] or string.format("ST%d", s.mbb_state)
  row(y, "MBB State",   mbb_name,                       false, false); y=y+9
  row(y, "Active Path", PATH_NAMES[s.active_path] or "?", false, false); y=y+9
  row(y, "Cand Path",   PATH_NAMES[s.candidate_path] or "NONE", false, false); y=y+9
  row(y, "Link A",      string.format("LQ:%d RSSI:%d", s.lq_a, s.rssi_a), false, false); y=y+9
  row(y, "Link B",      string.format("LQ:%d RSSI:%d", s.lq_b, s.rssi_b), false, false); y=y+9
  local audit_str = (s.audit_status == 0) and "OK" or (s.audit_status == 1) and "WARN" or "FAIL"
  row(y, "Audit Log",   audit_str, false, false); y=y+9
  lcd.drawText(COL1, LCD_H-9, "[MENU]=Back  [R/L]=Page", SMLSIZE)
end

local ZONE_NAMES = { [0]="LOW", [1]="MID", [2]="HIGH" }

local function draw_transition(cursor, edit)
  local y = 0
  lcd.drawText(0, y, "RF Transition", BOLD); y = y + 9

  local pzone = s.pending_zone or s.zone_secondary
  row(y, "Target Zone",  ZONE_NAMES[pzone] or "?", cursor==1, edit); y=y+9
  row(y, "Status",       s.trans_state,               false, false); y=y+9

  if s.trans_state == "IDLE" then
    lcd.drawText(COL1, y, "[ENT]=Request Transition", SMLSIZE)
  elseif s.trans_state == "EXECUTING" then
    local mbb_name = MBB_STATES[s.mbb_state] or "..."
    lcd.drawText(COL1, y, "FW State: " .. mbb_name, SMLSIZE); y=y+9
    lcd.drawText(COL1, y, "[MENU]=Abort", SMLSIZE)
  elseif s.trans_state == "DONE" then
    lcd.drawText(COL1, y, "SUCCESS - now " .. (PATH_NAMES[s.active_path] or "?"), 0)
  elseif s.trans_state == "ROLLBACK" then
    lcd.drawText(COL1, y, "ROLLED BACK - primary ok", 0)
  elseif s.trans_state == "FAILSAFE" then
    lcd.drawText(COL1, y, "FAILSAFE HOLD!", BLINK)
  end
  lcd.drawText(COL1, LCD_H-9, "[MENU]=Back  [R/L]=Page", SMLSIZE)
end

-- ── Input handling ─────────────────────────────────────────────────────────
local MAX_CURSOR = { [1]=4, [2]=0, [3]=1 }

local function handle_input_config(event)
  if event == EVT_VIRTUAL_ENTER then
    s.editing = not s.editing
    if not s.editing then
      -- Commit change: write param to firmware
      if s.cursor == 1 then push("ZONE_PRIMARY",   s.zone_primary)
      elseif s.cursor == 2 then push("ZONE_SECONDARY", s.zone_secondary)
      elseif s.cursor == 3 then push("TRANS_POLICY",   s.trans_policy)
      elseif s.cursor == 4 then push("GUARD_BAND",     s.guard_band)
      end
    end
  elseif event == EVT_VIRTUAL_NEXT then
    if s.editing then
      if s.cursor == 1 then s.zone_primary    = (s.zone_primary + 1) % 3
      elseif s.cursor == 2 then s.zone_secondary = (s.zone_secondary + 1) % 3
      elseif s.cursor == 3 then s.trans_policy   = (s.trans_policy + 1) % 3
      elseif s.cursor == 4 then s.guard_band     = math.min(s.guard_band + 1, 20)
      end
    else
      s.cursor = math.min(s.cursor + 1, MAX_CURSOR[s.page])
    end
  elseif event == EVT_VIRTUAL_PREV then
    if s.editing then
      if s.cursor == 1 then s.zone_primary    = (s.zone_primary + 2) % 3
      elseif s.cursor == 2 then s.zone_secondary = (s.zone_secondary + 2) % 3
      elseif s.cursor == 3 then s.trans_policy   = (s.trans_policy + 2) % 3
      elseif s.cursor == 4 then s.guard_band     = math.max(s.guard_band - 1, 1)
      end
    else
      s.cursor = math.max(s.cursor - 1, 1)
    end
  end
end

local function handle_input_transition(event)
  if event == EVT_VIRTUAL_ENTER then
    if s.trans_state == "IDLE" then
      -- Request transition (+1 because 0=ABORT, 1..3 = zone LOW/MID/HIGH)
      local target = s.pending_zone or s.zone_secondary
      send_transition_cmd(target + 1)
      s.trans_state = "EXECUTING"
    elseif s.trans_state == "EXECUTING" then
      -- Abort
      send_transition_cmd(0)
      s.trans_state = "IDLE"
    elseif s.trans_state == "DONE" or s.trans_state == "ROLLBACK" or s.trans_state == "FAILSAFE" then
      s.trans_state = "IDLE"
    end
  elseif event == EVT_VIRTUAL_NEXT and s.trans_state == "IDLE" then
    local pz = s.pending_zone or s.zone_secondary
    s.pending_zone = (pz + 1) % 3
  elseif event == EVT_VIRTUAL_PREV and s.trans_state == "IDLE" then
    local pz = s.pending_zone or s.zone_secondary
    s.pending_zone = (pz + 2) % 3
  end
end

-- ── Main run/background functions ─────────────────────────────────────────────
local function init()
  s.last_tick = getTime()
  s_disc_last_t = s.last_tick
  crossfireTelemetryPush(0x2B, {deviceId, handsetId, 0, 0})
end

local function run(event)
  -- Page switching (right/left when not editing)
  if not s.editing then
    if event == EVT_VIRTUAL_RIGHT then s.page = math.min(s.page + 1, 3); s.cursor = 1 end
    if event == EVT_VIRTUAL_LEFT  then s.page = math.max(s.page - 1, 1); s.cursor = 1 end
  end

  -- Inbound telemetry (status frames arrive continuously from devRFProfile)
  -- crossfireTelemetryPop() returns (frame_type, data); frame_type is nil when empty.
  local frame_type, data = crossfireTelemetryPop()
  if frame_type then parse_response(frame_type, data) end

  -- Drive parameter discovery until all 4 indices are found
  if not s_disc_done then
    local now = getTime()
    if now - s_disc_last_t >= 5 then  -- 50 ms (getTime unit = 10 ms)
      s_disc_last_t = now
      crossfireTelemetryPush(0x2B, {deviceId, handsetId, s_disc_chunk, 0})
    end
  end

  -- Draw
  lcd.clear()
  if s.page == 1 then
    draw_config(s.cursor, s.editing)
    handle_input_config(event)
  elseif s.page == 2 then
    draw_status(s.cursor)
  elseif s.page == 3 then
    draw_transition(s.cursor, s.editing)
    handle_input_transition(event)
  end

  -- Page indicator
  lcd.drawText(LCD_W - 20, 0, string.format("%d/3", s.page), SMLSIZE)

  return 0
end

return { init=init, run=run }
