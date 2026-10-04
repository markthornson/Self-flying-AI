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

## Next steps

1. DCS bridge: `Export.lua` sending telemetry over UDP and applying stick and
   throttle via `LoSetCommand`, plus a `DcsF16Env` that builds the same
   `FlightState`.
2. Fly the JSBSim-trained policy in DCS unchanged, measure the gap, fine-tune.
