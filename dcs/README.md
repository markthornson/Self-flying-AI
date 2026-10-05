# Flying in DCS

## 1. Install the export script

1. Copy `dcs/SelfFlyingExport.lua` to `Saved Games\DCS\Scripts\`
   (for the open beta it is `Saved Games\DCS.openbeta\Scripts\`).
2. Open `Saved Games\DCS\Scripts\Export.lua` (create it if it doesn't exist)
   and add this line at the end:

   ```lua
   dofile(lfs.writedir() .. [[Scripts\SelfFlyingExport.lua]])
   ```

   Other exporters in that file (Tacview, SRS) keep working; the script
   chains onto them.
3. Start DCS. `Saved Games\DCS\Logs\dcs.log` should contain
   `SELFFLY ... bridge started` once a mission loads.

The script listens for controls on UDP 127.0.0.1:7779 and sends telemetry to
127.0.0.1:7778. Export scripts only run for your own aircraft, in single
player or on servers that allow exports.

## 2. Air-start mission

Make a mission with a single player F-16C, air start at about 15,000 ft and
380 kts, wings level, over flat terrain or sea, no other aircraft. Save it as
`dcs/missions/airstart.miz` so restarts are quick.

## 3. Milestone M0 check

On the same PC, with Python 3.11 and `pip install -r requirements.txt`:

```bash
python -m tools.dcs_check
```

Fly the mission wings level above 8,000 ft AGL and let go of the stick. The
tool prints live telemetry; compare altitude, speed, heading, pitch and bank
with the HUD. Then it nudges pitch, roll, rudder and throttle in turn for a
second each, works out which way each axis goes, and saves
`dcs_calibration.json`. Expect the jet to wobble a little.

If the jet doesn't respond at all, the F-16 module is ignoring
`LoSetCommand` axis inputs; the fallback is a vJoy virtual joystick.

## 4. Fly the trained policy

```bash
python -m tools.fly_dcs runs/ppo_f16/final.zip --hold 90 15000 380
python -m tools.fly_dcs runs/ppo_f16/final.zip --route dcs/routes/example.json
```

It hands control back on Ctrl+C, when the time is up, or if the jet gets near
the envelope limits (below 1,000 ft AGL, AoA over 30°, over 9 g). Touching the
stick yourself doesn't take control back; stop the script first. Each flight
is logged to `dcs_flight.csv`.

## Testing without DCS

`tools/fake_dcs.py` runs JSBSim's F-16 behind the same UDP protocol:

```bash
python -m tools.fake_dcs &
python -m tools.dcs_check
```
