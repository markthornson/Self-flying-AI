"""A stand-in for DCS that speaks the export script's UDP protocol.

Runs JSBSim's F-16 in real time (or faster with --speed) and behaves like
dcs/SelfFlyingExport.lua: telemetry out at 20 Hz in DCS units, controls in,
and a 0.5 s watchdog that hands control back to a "pilot" holding trim.
Used to test the Python bridge without DCS:

    python -m tools.fake_dcs --speed 1
"""

import argparse
import socket
import time

import jsbsim
import numpy as np

from dcsai.envs.jsbsim_f16 import GCommandLoop

FT_TO_M = 0.3048
KTS_TO_MS = 0.514444


class FakeDcs:
    def __init__(self, host="127.0.0.1", telemetry_port=7778, control_port=7779, signs=(1, 1, 1, -1),
                 alt_ft=15000, kias=380, heading_deg=0):
        self.telemetry_addr = (host, telemetry_port)
        self.rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx.bind((host, control_port))
        self.rx.setblocking(False)
        self.tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # How this fake "DCS" interprets LoSetCommand values; see Calibration.
        self.pitch_sign, self.roll_sign, self.rudder_sign, self.throttle_sign = signs

        f = self.fdm = jsbsim.FGFDMExec(None)
        f.set_debug_level(0)
        f.load_model("f16")
        f.set_dt(1.0 / 120)
        f["ic/lat-geod-deg"] = 42.0
        f["ic/long-gc-deg"] = 42.0
        f["ic/terrain-elevation-ft"] = 0.0
        f["ic/h-sl-ft"] = alt_ft
        f["ic/psi-true-deg"] = heading_deg
        f["ic/vc-kts"] = kias
        f.run_ic()
        f["propulsion/set-running"] = -1
        f.do_trim(1)
        self.trim = {k: f[k] for k in ("fcs/elevator-cmd-norm", "fcs/aileron-cmd-norm",
                                        "fcs/rudder-cmd-norm", "fcs/throttle-cmd-norm")}
        # Like the DCS Viper, the stick commands g rather than elevator.
        self.g_loop = GCommandLoop(f)
        self.seq = 0
        self.last_control = -1e9
        self.control = None
        self.running = True

    def _receive(self, now):
        while True:
            try:
                packet = self.rx.recv(512).decode("ascii")
            except BlockingIOError:
                break
            parts = packet.split(",")
            if parts[0] == "C" and len(parts) == 6:
                self.control = [float(v) for v in parts[2:]]
                self.last_control = now
        return self.control is not None and now - self.last_control < 0.5

    def _apply(self, active):
        f = self.fdm
        if not active:
            for k, v in self.trim.items():
                f[k] = v
            return
        pitch, roll, rudder, thrust = self.control
        self.g_loop.update(float(np.clip(pitch * self.pitch_sign, -1.0, 1.0)))
        f["fcs/aileron-cmd-norm"] = roll * self.roll_sign
        f["fcs/rudder-cmd-norm"] = rudder * self.rudder_sign
        f["fcs/throttle-cmd-norm"] = (thrust * self.throttle_sign + 1.0) / 2.0

    def _telemetry(self, active):
        f = self.fdm
        self.seq += 1
        fields = [
            self.seq, f.get_sim_time(),
            f["position/lat-geod-deg"], f["position/long-gc-deg"],
            f["position/h-sl-ft"] * FT_TO_M, f["position/h-agl-ft"] * FT_TO_M,
            f["attitude/psi-rad"], f["attitude/theta-rad"], f["attitude/phi-rad"],
            f["velocities/vc-kts"] * KTS_TO_MS, f["velocities/mach"],
            f["aero/alpha-rad"], f["aero/beta-rad"],
            -f["accelerations/n-pilot-z-norm"], f["velocities/h-dot-fps"] * FT_TO_M,
            # DCS body axes: x forward, y up, z right.
            f["velocities/p-rad_sec"], -f["velocities/r-rad_sec"], f["velocities/q-rad_sec"],
            1 if active else 0,
        ]
        line = "T," + ",".join(f"{v:.6f}" if isinstance(v, float) else str(v) for v in fields)
        self.tx.sendto(line.encode("ascii"), self.telemetry_addr)

    def run(self, seconds=None, speed=1.0):
        start = time.monotonic()
        sim_steps = 0
        active = False
        while self.running and (seconds is None or self.fdm.get_sim_time() < seconds):
            now = time.monotonic()
            active = self._receive(self.fdm.get_sim_time())
            self._apply(active)
            self.fdm.run()
            sim_steps += 1
            if sim_steps % 6 == 0:  # 20 Hz
                self._telemetry(active)
            target = start + self.fdm.get_sim_time() / speed
            if target > now:
                time.sleep(target - now)

    def close(self):
        self.running = False
        self.rx.close()
        self.tx.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--speed", type=float, default=1.0)
    parser.add_argument("--seconds", type=float)
    args = parser.parse_args()
    FakeDcs().run(args.seconds, args.speed)


if __name__ == "__main__":
    main()
