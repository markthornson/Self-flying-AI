"""Gymnasium environment: hold a heading/altitude/speed command in JSBSim's F-16."""

import gymnasium as gym
import jsbsim
import numpy as np

from dcsai.spec import (
    ACTION_DIM,
    OBS_DIM,
    Command,
    FlightState,
    build_observation,
    is_failed,
    step_reward,
)

SIM_HZ = 120
AGENT_HZ = 10
CRASH_REWARD = -20.0


class JsbsimF16Env(gym.Env):
    """Air-started F-16 that must track commands which change during the episode.

    Randomised each episode: start altitude, speed, heading and attitude, wind,
    control latency and sensor noise, so the policy doesn't overfit JSBSim.
    """

    metadata = {"render_modes": []}

    def __init__(self, episode_seconds=120.0, randomize=True):
        self.episode_steps = int(episode_seconds * AGENT_HZ)
        self.randomize = randomize
        self.observation_space = gym.spaces.Box(-5.0, 5.0, (OBS_DIM,), np.float32)
        self.action_space = gym.spaces.Box(-1.0, 1.0, (ACTION_DIM,), np.float32)
        self.fdm = None

    # -- JSBSim plumbing -------------------------------------------------

    def _new_fdm(self):
        fdm = jsbsim.FGFDMExec(None)
        fdm.set_debug_level(0)
        fdm.load_model("f16")
        fdm.set_dt(1.0 / SIM_HZ)
        return fdm

    def read_state(self) -> FlightState:
        f = self.fdm
        return FlightState(
            roll_rad=f["attitude/phi-rad"],
            pitch_rad=f["attitude/theta-rad"],
            heading_rad=f["attitude/psi-rad"],
            p_rad_s=f["velocities/p-rad_sec"],
            q_rad_s=f["velocities/q-rad_sec"],
            r_rad_s=f["velocities/r-rad_sec"],
            kias=f["velocities/vc-kts"],
            mach=f["velocities/mach"],
            alpha_deg=f["aero/alpha-deg"],
            beta_deg=f["aero/beta-deg"],
            nz_g=-f["accelerations/n-pilot-z-norm"],
            vs_fps=f["velocities/h-dot-fps"],
            alt_ft=f["position/h-sl-ft"],
            agl_ft=f["position/h-agl-ft"],
            throttle=f["fcs/throttle-cmd-norm"],
        )

    def position(self):
        return self.fdm["position/lat-geod-deg"], self.fdm["position/long-gc-deg"]

    def _apply(self, action):
        self.fdm["fcs/elevator-cmd-norm"] = float(-action[0])  # stick back = nose up
        self.fdm["fcs/aileron-cmd-norm"] = float(action[1])
        self.fdm["fcs/rudder-cmd-norm"] = float(action[2])
        self.fdm["fcs/throttle-cmd-norm"] = float((action[3] + 1.0) / 2.0)

    # -- Episode logic ---------------------------------------------------

    def _random_command(self, state: FlightState) -> Command:
        rng = self.np_random
        return Command(
            heading_deg=float((np.degrees(state.heading_rad) + rng.uniform(-180, 180)) % 360),
            alt_ft=float(np.clip(state.alt_ft + rng.uniform(-4000, 4000), 6000, 30000)),
            kias=float(np.clip(state.kias + rng.uniform(-100, 100), 280, 500)),
        )

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)
        rng = self.np_random
        options = options or {}
        for attempt in range(5):
            # A fresh model per episode (~7 ms) so no integrator state leaks between episodes.
            self.fdm = f = self._new_fdm()
            f["ic/lat-geod-deg"] = options.get("lat_deg", 42.0)
            f["ic/long-gc-deg"] = options.get("lon_deg", 42.0)
            f["ic/terrain-elevation-ft"] = 0.0
            f["ic/h-sl-ft"] = options.get("alt_ft", rng.uniform(8000, 25000))
            f["ic/psi-true-deg"] = options.get("heading_deg", rng.uniform(0, 360))
            # Airspeed last: setting position afterwards would recompute it from true airspeed.
            f["ic/vc-kts"] = options.get("kias", rng.uniform(300, 450))
            f["ic/phi-deg"] = 0.0
            f["ic/theta-deg"] = 0.0
            f.run_ic()
            f["propulsion/set-running"] = -1
            try:
                f.do_trim(1)
                break
            except jsbsim.TrimFailureError:
                if attempt == 4 or options:
                    raise

        if self.randomize:
            # Upset after trim so the policy learns to recover, not just hold.
            f["ic/phi-deg"] = rng.uniform(-30, 30)
            f["ic/theta-deg"] = f["attitude/theta-deg"] + rng.uniform(-5, 5)
            f["ic/vc-kts"] = f["velocities/vc-kts"]
            f["ic/h-sl-ft"] = f["position/h-sl-ft"]
            f.run_ic()
            wind = rng.uniform(0, 60)
            wind_dir = rng.uniform(0, 2 * np.pi)
            f["atmosphere/wind-north-fps"] = wind * np.cos(wind_dir)
            f["atmosphere/wind-east-fps"] = wind * np.sin(wind_dir)
            self.latency_steps = int(rng.integers(0, 2))
            self.obs_noise = rng.uniform(0.0, 0.02)
        else:
            self.latency_steps = 0
            self.obs_noise = 0.0

        trim = np.array(
            [-f["fcs/elevator-cmd-norm"], f["fcs/aileron-cmd-norm"], f["fcs/rudder-cmd-norm"],
             f["fcs/throttle-cmd-norm"] * 2.0 - 1.0],
            dtype=np.float32,
        )
        self.prev_action = trim
        self.pending = [trim] * self.latency_steps
        self.steps = 0
        state = self.read_state()
        self.command = options.get("command") or self._random_command(state)
        self.next_command_step = self._schedule_next_command()
        return self._observe(state), {"command": self.command}

    def _schedule_next_command(self):
        return self.steps + int(self.np_random.uniform(30, 60) * AGENT_HZ)

    def _observe(self, state):
        obs = build_observation(state, self.command, self.prev_action)
        if self.obs_noise:
            obs = obs + self.np_random.normal(0, self.obs_noise, obs.shape).astype(np.float32)
        return obs

    def set_command(self, command: Command):
        """Override the target, e.g. from a RouteFollower. Stops random re-targeting."""
        self.command = command
        self.next_command_step = None

    def step(self, action):
        action = np.clip(np.asarray(action, dtype=np.float32), -1.0, 1.0)
        self.pending.append(action)
        applied = self.pending.pop(0)
        self._apply(applied)
        for _ in range(SIM_HZ // AGENT_HZ):
            self.fdm.run()
        self.steps += 1

        state = self.read_state()
        failed = is_failed(state)
        reward = CRASH_REWARD if failed else step_reward(state, self.command, action, self.prev_action)
        self.prev_action = action

        if self.next_command_step is not None and self.steps >= self.next_command_step:
            self.command = self._random_command(state)
            self.next_command_step = self._schedule_next_command()

        truncated = self.steps >= self.episode_steps
        info = {"command": self.command, "state": state}
        return self._observe(state), float(reward), failed, truncated, info
