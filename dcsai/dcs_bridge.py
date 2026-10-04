"""Python side of the DCS bridge (see dcs/SelfFlyingExport.lua).

DcsLink talks UDP to the export script. DcsF16Env wraps it in the same
Gymnasium interface as JsbsimF16Env, so a policy trained in JSBSim can fly
DCS unchanged.
"""

import json
import socket
import threading
import time
from dataclasses import asdict, dataclass
from pathlib import Path

import gymnasium as gym
import numpy as np

from dcsai.spec import ACTION_DIM, OBS_DIM, Command, FlightState, build_observation, is_failed

M_TO_FT = 3.280839895
MS_TO_KTS = 1.943844492

TELEMETRY_FIELDS = (
    "seq", "model_time", "lat", "lon", "alt_m", "agl_m", "heading", "pitch", "bank",
    "ias_ms", "mach", "aoa", "aos", "nz_g", "vs_ms", "wx", "wy", "wz", "ai_in_control",
)


@dataclass
class Telemetry:
    seq: int
    model_time: float
    lat: float
    lon: float
    alt_m: float
    agl_m: float
    heading: float
    pitch: float
    bank: float
    ias_ms: float
    mach: float
    aoa: float
    aos: float
    nz_g: float
    vs_ms: float
    wx: float
    wy: float
    wz: float
    ai_in_control: bool
    received_at: float = 0.0

    @classmethod
    def parse(cls, packet: bytes) -> "Telemetry":
        parts = packet.decode("ascii").strip().split(",")
        if parts[0] != "T" or len(parts) != len(TELEMETRY_FIELDS) + 1:
            raise ValueError(f"bad telemetry packet: {packet[:80]!r}")
        values = [float(v) for v in parts[1:]]
        kwargs = dict(zip(TELEMETRY_FIELDS, values))
        kwargs["seq"] = int(kwargs["seq"])
        kwargs["ai_in_control"] = kwargs["ai_in_control"] > 0.5
        return cls(**kwargs, received_at=time.monotonic())


@dataclass
class Calibration:
    """Sign conventions that differ between DCS and JSBSim.

    The defaults are best guesses from the DCS export API. `python -m
    tools.dcs_check` measures the control signs in a live DCS session and
    saves them to dcs_calibration.json; the telemetry signs are checked by
    eye from its printout.
    """

    pitch_sign: float = 1.0  # +1 if LoSetCommand(2001, +x) pitches the nose up
    roll_sign: float = 1.0  # +1 if LoSetCommand(2002, +x) rolls right
    rudder_sign: float = 1.0  # +1 if LoSetCommand(2003, +x) yaws left, like JSBSim's rudder-cmd-norm
    throttle_sign: float = -1.0  # +1 if LoSetCommand(2004, +1) is full thrust, -1 if it is idle
    aoa_in_degrees: bool = False  # LoGetAngleOfAttack units

    @classmethod
    def load(cls, path="dcs_calibration.json"):
        p = Path(path)
        return cls(**json.loads(p.read_text())) if p.exists() else cls()

    def save(self, path="dcs_calibration.json"):
        Path(path).write_text(json.dumps(asdict(self), indent=2))


def to_flight_state(t: Telemetry, throttle: float, cal: Calibration) -> FlightState:
    """Convert DCS units and axes to the shared FlightState.

    DCS body axes are x forward, y up, z right, so roll rate is wx, pitch rate
    (nose up) is wz, and yaw rate (nose right) is -wy.
    """
    aoa_deg = t.aoa if cal.aoa_in_degrees else np.degrees(t.aoa)
    aos_deg = t.aos if cal.aoa_in_degrees else np.degrees(t.aos)
    return FlightState(
        roll_rad=t.bank,
        pitch_rad=t.pitch,
        heading_rad=t.heading,
        p_rad_s=t.wx,
        q_rad_s=t.wz,
        r_rad_s=-t.wy,
        kias=t.ias_ms * MS_TO_KTS,
        mach=t.mach,
        alpha_deg=float(aoa_deg),
        beta_deg=float(aos_deg),
        nz_g=t.nz_g,
        vs_fps=t.vs_ms * M_TO_FT,
        alt_ft=t.alt_m * M_TO_FT,
        agl_ft=t.agl_m * M_TO_FT,
        throttle=throttle,
    )


class DcsLink:
    """UDP link to the export script. Keeps the newest telemetry packet."""

    def __init__(self, host="127.0.0.1", telemetry_port=7778, control_port=7779):
        self.control_addr = (host, control_port)
        self.rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx.bind((host, telemetry_port))
        self.rx.settimeout(0.2)
        self.tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.latest = None
        self.bad_packets = 0
        self._new = threading.Condition()
        self._seq = 0
        self._running = True
        self._thread = threading.Thread(target=self._listen, daemon=True)
        self._thread.start()

    def _listen(self):
        while self._running:
            try:
                packet = self.rx.recv(2048)
            except (socket.timeout, OSError):
                continue
            try:
                t = Telemetry.parse(packet)
            except ValueError:
                self.bad_packets += 1
                continue
            with self._new:
                self.latest = t
                self._new.notify_all()

    def wait_for_telemetry(self, newer_than=-1, timeout=5.0) -> Telemetry:
        deadline = time.monotonic() + timeout
        with self._new:
            while self.latest is None or self.latest.seq <= newer_than:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("no telemetry from DCS; is the mission running with the export script installed?")
                self._new.wait(remaining)
            return self.latest

    def send_controls(self, pitch, roll, rudder, throttle):
        self._seq += 1
        packet = f"C,{self._seq},{pitch:.4f},{roll:.4f},{rudder:.4f},{throttle:.4f}"
        self.tx.sendto(packet.encode("ascii"), self.control_addr)

    def close(self):
        self._running = False
        self._thread.join(timeout=1.0)
        self.rx.close()
        self.tx.close()


class DcsF16Env(gym.Env):
    """Flies the player's F-16 in a running DCS mission.

    There is no simulator reset: reset() just takes over from wherever the jet
    is. Start an air-start mission, then reset(). When the episode ends, the
    env stops sending controls and the export script hands the stick back.
    """

    metadata = {"render_modes": []}

    def __init__(self, link: DcsLink, command: Command, calibration: Calibration = None, agent_hz=10.0):
        self.link = link
        self.command = command
        self.cal = calibration or Calibration()
        self.period = 1.0 / agent_hz
        self.observation_space = gym.spaces.Box(-5.0, 5.0, (OBS_DIM,), np.float32)
        self.action_space = gym.spaces.Box(-1.0, 1.0, (ACTION_DIM,), np.float32)

    def set_command(self, command: Command):
        self.command = command

    def observe(self):
        """Observation for the latest telemetry and current command."""
        return build_observation(self.read_state(), self.command, self.prev_action)

    def position(self):
        return self.telemetry.lat, self.telemetry.lon

    def read_state(self) -> FlightState:
        return to_flight_state(self.telemetry, self.throttle, self.cal)

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)
        self.telemetry = self.link.wait_for_telemetry()
        # Unknown until we command it; assume mid-range so the first observation is sane.
        self.throttle = (options or {}).get("throttle", 0.75)
        self.prev_action = np.array([0.0, 0.0, 0.0, self.throttle * 2 - 1], dtype=np.float32)
        self.next_tick = time.monotonic()
        state = self.read_state()
        return build_observation(state, self.command, self.prev_action), {"state": state, "telemetry": self.telemetry}

    def step(self, action):
        action = np.clip(np.asarray(action, dtype=np.float32), -1.0, 1.0)
        pitch, roll, rudder, throttle = (float(a) for a in action)
        self.link.send_controls(
            pitch * self.cal.pitch_sign,
            roll * self.cal.roll_sign,
            rudder * self.cal.rudder_sign,
            throttle * self.cal.throttle_sign,
        )
        self.throttle = (throttle + 1.0) / 2.0
        self.prev_action = action

        self.next_tick += self.period
        delay = self.next_tick - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        else:
            self.next_tick = time.monotonic()  # fell behind; don't try to catch up
        self.telemetry = self.link.wait_for_telemetry(newer_than=-1, timeout=2.0)

        state = self.read_state()
        failed = is_failed(state)
        return build_observation(state, self.command, self.prev_action), 0.0, failed, False, {
            "state": state, "telemetry": self.telemetry, "command": self.command,
        }
