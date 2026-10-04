# game-engine: self-flying F-16 for DCS World

A learned pilot for the F-16C in DCS World. The policy is trained in
[JSBSim](https://github.com/JSBSim-Team/jsbsim)'s F-16 model, which runs hundreds
of times faster than real time, and will later fly in DCS through a Lua export
bridge using the same observation and action spec.

## Layout

| Path | What it is |
|------|------------|
| `dcsai/spec.py` | Simulator-independent `FlightState`, `Command`, observation, reward and failure checks. The DCS bridge will reuse this unchanged. |
| `dcsai/guidance.py` | Waypoint route → heading/altitude/speed `Command`s. |
| `dcsai/envs/jsbsim_f16.py` | `JsbsimF16Env`, a Gymnasium env: hold a command that changes every 30 to 60 s. |
| `train/train_ppo.py` | PPO training (Stable-Baselines3). |
| `train/evaluate.py` | Tracking errors on random commands, or a 4-waypoint route. |
| `dcs/SelfFlyingExport.lua` | DCS export script: telemetry out, stick and throttle in, over UDP. |
| `dcsai/dcs_bridge.py` | `DcsLink` (UDP) and `DcsF16Env`, the DCS twin of `JsbsimF16Env`. |
| `tools/dcs_check.py` | Milestone M0 check: live telemetry and control-direction calibration. |
| `tools/fly_dcs.py` | Fly a trained policy in DCS, holding a command or following a route. |
| `tools/fake_dcs.py` | JSBSim behind the export script's protocol, for testing without DCS. |

## How the pilot works

The policy outputs four actions in [-1, 1] at 10 Hz: stick pitch, stick roll,
rudder, throttle. It sees 24 normalised inputs: attitude, body rates, airspeed,
Mach, AoA, sideslip, g, vertical speed, height above ground, throttle, the error
to the commanded heading/altitude/speed, and its previous action. It never sees
absolute position, so the same policy works anywhere.

Each episode starts from a trimmed air start with a random bank upset, wind,
0 to 100 ms of control latency and sensor noise, so the policy doesn't overfit
JSBSim before it moves to DCS. Episodes end on a crash (below 1,000 ft AGL),
AoA over 30° or more than 9 g.

## Usage

```bash
pip install -r requirements.txt
python -m pytest -q
python -m train.train_ppo --steps 4000000 --envs 8 --out runs/ppo_f16
python -m train.evaluate runs/ppo_f16/final.zip
python -m train.evaluate runs/ppo_f16/final.zip --route
```

## Flying in DCS

See [dcs/README.md](dcs/README.md) for installing the export script, the M0
check and flying a trained policy.
