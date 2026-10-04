"""Evaluate a trained pilot in JSBSim.

    python -m train.evaluate runs/ppo_f16/final.zip            # random command changes
    python -m train.evaluate runs/ppo_f16/final.zip --route    # 4-waypoint route
"""

import argparse

import numpy as np
from stable_baselines3 import PPO

from dcsai.envs import JsbsimF16Env
from dcsai.guidance import NM_FT, RouteFollower, Waypoint, offset_position
from dcsai.spec import heading_error_rad


def command_tracking(model, episodes, seed):
    env = JsbsimF16Env(episode_seconds=120.0, randomize=True)
    crashes, hdg, alt, spd = 0, [], [], []
    for ep in range(episodes):
        obs, _ = env.reset(seed=seed + ep)
        command, since_change = env.command, 0
        done = False
        while not done:
            action, _ = model.predict(obs, deterministic=True)
            obs, _, term, trunc, info = env.step(action)
            done = term or trunc
            s, c = info["state"], info["command"]
            since_change = 0 if c is not command else since_change + 1
            command = c
            # Score steady-state tracking: only once 30 s have passed since the command changed.
            if since_change < 30 * 10:
                continue
            hdg.append(abs(np.degrees(heading_error_rad(s, c))))
            alt.append(abs(c.alt_ft - s.alt_ft))
            spd.append(abs(c.kias - s.kias))
        crashes += int(term)
    print(f"{episodes} episodes of 120 s, commands change every 30-60 s; errors scored 30 s after each change")
    print(f"  crashes:               {crashes}/{episodes}")
    for name, errs, unit in (("heading", hdg, "deg"), ("altitude", alt, "ft"), ("airspeed", spd, "kts")):
        print(f"  {name:9s} error  median {np.median(errs):7.1f} {unit}   p90 {np.percentile(errs, 90):7.1f} {unit}")


def route(model, seed):
    lat0, lon0 = 42.0, 42.0
    legs = [(0, 15, 15000, 400), (90, 15, 18000, 380), (200, 20, 12000, 420), (300, 15, 15000, 350)]
    route, lat, lon = [], lat0, lon0
    for bearing, dist_nm, alt_ft, kias in legs:
        lat, lon = offset_position(lat, lon, bearing, dist_nm * NM_FT)
        route.append(Waypoint(lat, lon, alt_ft, kias))

    env = JsbsimF16Env(episode_seconds=900.0, randomize=False)
    follower = RouteFollower(route, capture_nm=1.0)
    obs, _ = env.reset(seed=seed, options=dict(alt_ft=15000, kias=400, heading_deg=0, lat_deg=lat0, lon_deg=lon0))
    captured_at = []
    done = term = False
    while not done:
        before = follower.index
        cmd = follower.update(*env.position())
        if follower.index != before:
            s = env.read_state()
            captured_at.append((env.steps / 10.0, s.alt_ft, s.kias))
        if cmd is None:
            break
        env.set_command(cmd)
        obs = env._observe(env.read_state())
        action, _ = model.predict(obs, deterministic=True)
        obs, _, term, trunc, _ = env.step(action)
        done = term or trunc

    print(f"route: {len(captured_at)}/{len(route)} waypoints captured (1 nm radius)")
    for i, (t, alt, kias) in enumerate(captured_at):
        wp = route[i]
        print(f"  WP{i + 1} at {t:5.0f} s   alt {alt:6.0f} ft (target {wp.alt_ft:.0f})   {kias:4.0f} kts (target {wp.kias:.0f})")
    if term:
        print("  crashed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("model")
    parser.add_argument("--episodes", type=int, default=20)
    parser.add_argument("--seed", type=int, default=10_000)
    parser.add_argument("--route", action="store_true")
    args = parser.parse_args()
    model = PPO.load(args.model, device="cpu")
    if args.route:
        route(model, args.seed)
    else:
        command_tracking(model, args.episodes, args.seed)


if __name__ == "__main__":
    main()
