"""Simulator-independent observation, action and reward definitions.

Both the JSBSim training env and the DCS bridge convert their raw telemetry
into a FlightState and use the functions here, so a trained policy sees the
same inputs whichever simulator it is flying.
"""

from dataclasses import dataclass

import numpy as np

# Stick pitch (+ nose up), stick roll (+ right), rudder (+ nose left, JSBSim's convention),
# throttle (+1 full, -1 idle); each in [-1, 1].
ACTION_DIM = 4
OBS_DIM = 24

# Envelope limits that end an episode.
MIN_AGL_FT = 1000.0
MAX_ALPHA_DEG = 30.0
MAX_G = 9.0


@dataclass
class FlightState:
    roll_rad: float
    pitch_rad: float
    heading_rad: float  # true heading, 0 = north, clockwise
    p_rad_s: float  # body roll rate
    q_rad_s: float  # body pitch rate
    r_rad_s: float  # body yaw rate
    kias: float
    mach: float
    alpha_deg: float
    beta_deg: float
    nz_g: float  # normal load factor, +1 in level flight
    vs_fps: float  # vertical speed, positive up
    alt_ft: float  # above sea level
    agl_ft: float
    throttle: float  # 0..1


@dataclass
class Command:
    """What the pilot policy is asked to hold."""

    heading_deg: float
    alt_ft: float
    kias: float


def wrap_angle(rad):
    """Wrap an angle to [-pi, pi)."""
    return (rad + np.pi) % (2 * np.pi) - np.pi


def heading_error_rad(state: FlightState, cmd: Command) -> float:
    return float(wrap_angle(np.radians(cmd.heading_deg) - state.heading_rad))


def build_observation(state: FlightState, cmd: Command, prev_action) -> np.ndarray:
    hdg_err = heading_error_rad(state, cmd)
    alt_err = cmd.alt_ft - state.alt_ft
    spd_err = cmd.kias - state.kias
    obs = np.array(
        [
            np.sin(state.roll_rad),
            np.cos(state.roll_rad),
            state.pitch_rad / (np.pi / 2),
            state.p_rad_s / np.pi,
            state.q_rad_s / np.pi,
            state.r_rad_s / np.pi,
            (state.kias - 350.0) / 150.0,
            state.mach - 0.7,
            state.alpha_deg / 20.0,
            state.beta_deg / 10.0,
            (state.nz_g - 1.0) / 5.0,
            state.vs_fps / 200.0,
            state.agl_ft / 20000.0,
            state.throttle * 2.0 - 1.0,
            np.sin(hdg_err),
            np.cos(hdg_err),
            np.tanh(alt_err / 2000.0),
            np.tanh(alt_err / 300.0),
            np.tanh(spd_err / 100.0),
            np.tanh(spd_err / 20.0),
            *np.asarray(prev_action, dtype=np.float64),
        ],
        dtype=np.float32,
    )
    return np.clip(obs, -5.0, 5.0)


def tracking_reward(state: FlightState, cmd: Command) -> float:
    """Reward in [0, 1] for holding the commanded heading, altitude and speed."""
    hdg_err_deg = abs(np.degrees(heading_error_rad(state, cmd)))
    # The linear part keeps a gradient towards the target from any heading; a
    # Gaussian alone is flat when far off, and the policy learns never to turn.
    hdg = 0.5 * np.exp(-((hdg_err_deg / 15.0) ** 2)) + 0.5 * (1.0 - hdg_err_deg / 180.0)
    alt = np.exp(-(((cmd.alt_ft - state.alt_ft) / 400.0) ** 2))
    spd = np.exp(-(((cmd.kias - state.kias) / 30.0) ** 2))
    # Wings level once on heading, so the policy doesn't hold a bank angle.
    level = np.exp(-((np.degrees(state.roll_rad) / 30.0) ** 2)) if hdg_err_deg < 5 else 1.0
    return float((hdg + alt + spd) / 3.0 * (0.8 + 0.2 * level))


def smoothness_penalty(action, prev_action) -> float:
    return float(np.sum(np.abs(np.asarray(action) - np.asarray(prev_action))))


def envelope_penalty(state: FlightState) -> float:
    """Soft penalty for flying near the edge of the envelope."""
    pen = max(0.0, state.alpha_deg - 20.0) / 10.0
    pen += max(0.0, abs(state.nz_g - 1.0) - 5.0)  # from 6 g, ramping hard; the Viper's FCS limits at 9
    pen += max(0.0, 3000.0 - state.agl_ft) / 2000.0
    return float(pen)


def effort_penalty(action) -> float:
    """Discourage holding the stick or rudder at a stop. Throttle is free to sit at idle or full."""
    pitch, roll, rudder, _ = np.asarray(action, dtype=np.float64)
    return float(0.5 * pitch**2 + 0.5 * roll**2 + rudder**2)


def step_reward(state: FlightState, cmd: Command, action, prev_action) -> float:
    return (
        tracking_reward(state, cmd)
        - 0.1 * smoothness_penalty(action, prev_action)
        - 0.1 * effort_penalty(action)
        - 0.2 * envelope_penalty(state)
    )


def is_failed(state: FlightState) -> bool:
    """Crashed, departed controlled flight, or over-stressed the airframe."""
    values = [getattr(state, f) for f in state.__dataclass_fields__]
    return (
        not np.all(np.isfinite(values))
        or state.agl_ft < MIN_AGL_FT
        or abs(state.alpha_deg) > MAX_ALPHA_DEG
        or abs(state.nz_g) > MAX_G
    )
