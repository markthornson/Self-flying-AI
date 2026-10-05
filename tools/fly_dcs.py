"""Fly a trained policy in DCS (or tools.fake_dcs).

    python -m tools.fly_dcs runs/ppo_f16/final.zip --hold 90 15000 380
    python -m tools.fly_dcs runs/ppo_f16/final.zip --route route.json

A route file is a list of {"lat_deg", "lon_deg", "alt_ft", "kias"}. Press
Ctrl+C to stop; the export script hands control back within half a second.
"""

import argparse
import csv
import json
import time

import numpy as np
from stable_baselines3 import PPO

from dcsai.dcs_bridge import Calibration, DcsF16Env, DcsLink
from dcsai.guidance import RouteFollower, Waypoint
from dcsai.spec import Command, heading_error_rad


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("model")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--hold", nargs=3, type=float, metavar=("HEADING", "ALT_FT", "KIAS"))
    group.add_argument("--route")
    parser.add_argument("--seconds", type=float, default=300)
    parser.add_argument("--calibration", default="dcs_calibration.json")
    parser.add_argument("--log", default="dcs_flight.csv")
    args = parser.parse_args()

    model = PPO.load(args.model, device="cpu")
    follower = None
    if args.route:
        follower = RouteFollower([Waypoint(**wp) for wp in json.loads(open(args.route).read())])
        command = Command(0, 15000, 380)
    else:
        command = Command(*args.hold)

    link = DcsLink()
    env = DcsF16Env(link, command, Calibration.load(args.calibration))
    obs, info = env.reset()
    start = time.monotonic()
    with open(args.log, "w", newline="") as f:
        log = csv.writer(f)
        log.writerow(["t", "lat", "lon", "alt_ft", "kias", "hdg_deg", "roll_deg", "cmd_hdg", "cmd_alt", "cmd_kias",
                      "pitch", "roll", "rudder", "throttle"])
        try:
            while time.monotonic() - start < args.seconds:
                if follower:
                    cmd = follower.update(*env.position())
                    if cmd is None:
                        print("route complete")
                        break
                    env.set_command(cmd)
                    obs = env.observe()
                action, _ = model.predict(obs, deterministic=True)
                obs, _, failed, _, info = env.step(action)
                s, c = info["state"], info["command"]
                log.writerow([f"{time.monotonic() - start:.1f}", *env.position(), round(s.alt_ft), round(s.kias),
                              round(np.degrees(s.heading_rad) % 360, 1), round(np.degrees(s.roll_rad), 1),
                              round(c.heading_deg, 1), round(c.alt_ft), round(c.kias), *np.round(action, 3)])
                if failed:
                    print("envelope limit hit; handing control back")
                    break
                if int(time.monotonic() - start) % 5 == 0 and env.telemetry.seq % 20 == 0:
                    print(f"alt {s.alt_ft:6.0f}/{c.alt_ft:.0f} ft  {s.kias:4.0f}/{c.kias:.0f} kts  "
                          f"hdg err {np.degrees(heading_error_rad(s, c)):+6.1f} deg")
        except KeyboardInterrupt:
            print("stopped")
        finally:
            link.close()
    print(f"flight log saved to {args.log}")


if __name__ == "__main__":
    main()
