-- Headless MAME helper: snapshot at the emulated seconds in SIM_SNAP_AT
-- (comma separated), optionally holding pad buttons from SIM_PRESS
-- ("t:mask,t:mask" with mask bits as MAME port field names, e.g. "9:P1 A" (MAME names: P1 A/B/C, P1 Start, P1 Up...)),
-- then exit after the last snapshot.
local snap_at = {}
for s in string.gmatch(os.getenv("SIM_SNAP_AT") or "3,8", "[^,]+") do snap_at[#snap_at + 1] = tonumber(s) end
local presses = {}
for t, f in string.gmatch(os.getenv("SIM_PRESS") or "", "([%d%.]+):([^,]+)") do
  presses[#presses + 1] = { t = tonumber(t), field = f }
end
local idx = 1
local function now() return manager.machine.time:as_double() end
local function find_field(name)
  for _, port in pairs(manager.machine.ioport.ports) do
    for fname, field in pairs(port.fields) do
      if fname == name then return field end
    end
  end
end
emu.register_frame_done(function()
  local t = now()
  -- A field can be pressed several times: it's held if any of its presses is active.
  -- (keyed by name: each lookup returns a new wrapper object for the same field).
  local held, field = {}, {}
  for _, p in ipairs(presses) do
    p.f = p.f or find_field(p.field)
    if p.f then
      field[p.field] = p.f
      held[p.field] = held[p.field] or (t >= p.t and t < p.t + 0.2)
    end
  end
  for name, on in pairs(held) do field[name]:set_value(on and 1 or 0) end
  if snap_at[idx] and t >= snap_at[idx] then
    manager.machine.video:snapshot()
    idx = idx + 1
    if not snap_at[idx] then manager.machine:exit() end
  end
end)
