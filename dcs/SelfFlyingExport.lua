-- Self-flying AI bridge for DCS World.
--
-- Streams own-aircraft telemetry to a Python process over UDP and applies the
-- stick/rudder/throttle values it sends back. Install by copying this file to
--   Saved Games\DCS\Scripts\SelfFlyingExport.lua
-- and adding this line to the end of Saved Games\DCS\Scripts\Export.lua:
--   dofile(lfs.writedir() .. [[Scripts\SelfFlyingExport.lua]])
--
-- It chains any existing export callbacks (Tacview, SRS, ...), so it can sit
-- alongside other exporters.
--
-- Telemetry packet (to 127.0.0.1:7778), one CSV line, SI units / radians:
--   T,seq,model_time,lat,lon,alt_m,agl_m,heading,pitch,bank,ias_ms,mach,
--   aoa,aos,nz_g,vs_ms,wx,wy,wz,ai_in_control
-- Control packet (from Python, to port 7779):
--   C,seq,pitch,roll,rudder,throttle      each in [-1, 1]
-- Python stops controlling by sending no packets; after WATCHDOG_S seconds
-- the script stops overriding and the stick is the pilot's again.

local SelfFly = {
  host = "127.0.0.1",
  telemetry_port = 7778,
  control_port = 7779,
  send_interval = 1.0 / 20.0,
  WATCHDOG_S = 0.5,
}

package.path = package.path .. ";" .. lfs.currentdir() .. "/LuaSocket/?.lua"
package.cpath = package.cpath .. ";" .. lfs.currentdir() .. "/LuaSocket/?.dll"
local socket = require("socket")

-- DCS input command ids for analog axes.
local CMD_PITCH, CMD_ROLL, CMD_RUDDER, CMD_THRUST = 2001, 2002, 2003, 2004

local function clamp(v)
  if v ~= v then return 0 end -- NaN
  if v > 1 then return 1 end
  if v < -1 then return -1 end
  return v
end

function SelfFly.start()
  SelfFly.tx = socket.udp()
  SelfFly.tx:settimeout(0)
  SelfFly.tx:setpeername(SelfFly.host, SelfFly.telemetry_port)
  SelfFly.rx = socket.udp()
  SelfFly.rx:settimeout(0)
  SelfFly.rx:setsockname(SelfFly.host, SelfFly.control_port)
  SelfFly.seq = 0
  SelfFly.last_send = 0
  SelfFly.last_control = -1000
  SelfFly.control = nil
  log.write("SELFFLY", log.INFO, "bridge started")
end

function SelfFly.receive()
  local now = socket.gettime()
  while true do
    local packet = SelfFly.rx:receive()
    if not packet then break end
    local seq, p, r, y, t = string.match(packet, "^C,(%d+),([^,]+),([^,]+),([^,]+),([^,]+)")
    if seq then
      SelfFly.control = {
        pitch = clamp(tonumber(p) or 0),
        roll = clamp(tonumber(r) or 0),
        rudder = clamp(tonumber(y) or 0),
        throttle = clamp(tonumber(t) or 0),
      }
      SelfFly.last_control = now
    end
  end
  return SelfFly.control ~= nil and (now - SelfFly.last_control) < SelfFly.WATCHDOG_S
end

function SelfFly.apply()
  if not SelfFly.receive() then return false end
  local c = SelfFly.control
  LoSetCommand(CMD_PITCH, c.pitch)
  LoSetCommand(CMD_ROLL, c.roll)
  LoSetCommand(CMD_RUDDER, c.rudder)
  LoSetCommand(CMD_THRUST, c.throttle)
  return true
end

function SelfFly.send(ai_in_control)
  local now = socket.gettime()
  if now - SelfFly.last_send < SelfFly.send_interval then return end
  SelfFly.last_send = now

  local self_data = LoGetSelfData()
  if not self_data then return end
  local acc = LoGetAccelerationUnits() or { y = 1 }
  local w = LoGetAngularVelocity() or { x = 0, y = 0, z = 0 }
  SelfFly.seq = SelfFly.seq + 1
  local fields = {
    "T", SelfFly.seq, LoGetModelTime(),
    self_data.LatLongAlt.Lat, self_data.LatLongAlt.Long, self_data.LatLongAlt.Alt,
    LoGetAltitudeAboveGroundLevel() or 0,
    self_data.Heading, self_data.Pitch, self_data.Bank,
    LoGetIndicatedAirSpeed() or 0, LoGetMachNumber() or 0,
    LoGetAngleOfAttack() or 0, LoGetAngleOfSideSlip() or 0,
    acc.y, LoGetVerticalVelocity() or 0,
    w.x, w.y, w.z,
    ai_in_control and 1 or 0,
  }
  -- seq and the control flag stay integers; everything else gets 6 decimals.
  for i = 3, #fields - 1 do
    if type(fields[i]) == "number" then fields[i] = string.format("%.6f", fields[i]) end
  end
  SelfFly.tx:send(table.concat(fields, ","))
end

function SelfFly.stop()
  if SelfFly.tx then SelfFly.tx:close() end
  if SelfFly.rx then SelfFly.rx:close() end
  log.write("SELFFLY", log.INFO, "bridge stopped")
end

-- Chain onto any existing export callbacks.
local prev_start = LuaExportStart
local prev_before = LuaExportBeforeNextFrame
local prev_after = LuaExportAfterNextFrame
local prev_stop = LuaExportStop

LuaExportStart = function()
  if prev_start then pcall(prev_start) end
  local ok, err = pcall(SelfFly.start)
  if not ok then log.write("SELFFLY", log.ERROR, tostring(err)) end
end

LuaExportBeforeNextFrame = function()
  if prev_before then pcall(prev_before) end
  local ok, active = pcall(SelfFly.apply)
  SelfFly.active = ok and active
end

LuaExportAfterNextFrame = function()
  if prev_after then pcall(prev_after) end
  local ok, err = pcall(SelfFly.send, SelfFly.active)
  if not ok then log.write("SELFFLY", log.ERROR, tostring(err)) end
end

LuaExportStop = function()
  if prev_stop then pcall(prev_stop) end
  pcall(SelfFly.stop)
end
