"""Runs dcs/SelfFlyingExport.lua in Lua 5.1 (DCS's version) against stubbed DCS and socket APIs."""

from pathlib import Path

import pytest

lua51 = pytest.importorskip("lupa.lua51")

SCRIPT = Path(__file__).parent.parent / "dcs" / "SelfFlyingExport.lua"

STUBS = r"""
clock = 100.0
sent, commands, inbox, logs, chained = {}, {}, {}, {}, {}
lfs = { currentdir = function() return "C:/DCS" end }
log = { INFO = 1, ERROR = 2, write = function(_, _, msg) table.insert(logs, msg) end }

local function udp()
  local s = {}
  function s:settimeout() end
  function s:setpeername() end
  function s:setsockname() end
  function s:close() end
  function s:send(data) table.insert(sent, data) end
  function s:receive() return table.remove(inbox, 1) end
  return s
end
package.preload["socket"] = function()
  return { udp = udp, gettime = function() return clock end }
end

function LoSetCommand(id, value) commands[id] = value end
function LoGetModelTime() return 12.5 end
function LoGetSelfData()
  return { LatLongAlt = { Lat = 42.1, Long = 41.9, Alt = 4572.0 }, Heading = 1.57, Pitch = 0.05, Bank = -0.1 }
end
function LoGetAltitudeAboveGroundLevel() return 4500.0 end
function LoGetIndicatedAirSpeed() return 195.0 end
function LoGetMachNumber() return 0.72 end
function LoGetAngleOfAttack() return 0.05 end
function LoGetAngleOfSideSlip() return 0.0 end
function LoGetAccelerationUnits() return { x = 0, y = 1.02, z = 0 } end
function LoGetVerticalVelocity() return 1.5 end
function LoGetAngularVelocity() return { x = 0.01, y = 0.02, z = 0.03 } end

-- An exporter installed before ours (e.g. Tacview) that must keep working.
LuaExportStart = function() table.insert(chained, "start") end
LuaExportAfterNextFrame = function() table.insert(chained, "after") end
"""


@pytest.fixture
def lua():
    rt = lua51.LuaRuntime()
    rt.execute(STUBS)
    rt.execute(SCRIPT.read_text())
    rt.execute("LuaExportStart()")
    return rt


def frame(lua, dt=0.06):
    lua.execute(f"clock = clock + {dt}; LuaExportBeforeNextFrame(); LuaExportAfterNextFrame()")


def test_sends_telemetry_packet_with_all_fields(lua):
    frame(lua)
    packet = lua.eval("sent[#sent]")
    fields = packet.split(",")
    assert fields[0] == "T"
    assert len(fields) == 20
    assert float(fields[5]) == pytest.approx(4572.0)  # alt_m
    assert float(fields[10]) == pytest.approx(195.0)  # ias_ms
    assert fields[1] == "1"  # seq
    assert fields[-1] == "0"  # AI not in control yet


def test_applies_controls_and_clamps(lua):
    lua.execute('table.insert(inbox, "C,1,0.25,-0.5,0,2.0")')
    frame(lua)
    assert lua.eval("commands[2001]") == pytest.approx(0.25)
    assert lua.eval("commands[2002]") == pytest.approx(-0.5)
    assert lua.eval("commands[2004]") == pytest.approx(1.0)  # clamped
    assert lua.eval("sent[#sent]").endswith(",1")


def test_watchdog_hands_control_back(lua):
    lua.execute('table.insert(inbox, "C,1,0.25,0,0,0")')
    frame(lua)
    lua.execute("commands = {}")
    frame(lua, dt=0.6)  # no new control packet for > 0.5 s
    assert lua.eval("commands[2001]") is None
    assert lua.eval("sent[#sent]").endswith(",0")


def test_ignores_malformed_packets(lua):
    lua.execute('table.insert(inbox, "garbage"); table.insert(inbox, "C,1,abc,0,0,0")')
    frame(lua)
    assert lua.eval("commands[2001]") == 0  # parsed row, bad number became 0


def test_throttles_telemetry_to_20hz(lua):
    for _ in range(10):
        frame(lua, dt=0.01)  # 100 fps for 0.1 s
    assert lua.eval("#sent") == 2


def test_chains_existing_exporters(lua):
    frame(lua)
    assert lua.eval("chained[1]") == "start"
    assert lua.eval("chained[2]") == "after"
