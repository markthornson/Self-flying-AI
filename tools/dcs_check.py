"""Milestone M0 check: is the bridge working, and which way do the controls go?

Run with DCS in an air-start F-16 mission at 10,000 ft or higher, wings level,
with dcs/SelfFlyingExport.lua installed:

    python -m tools.dcs_check

It prints live telemetry (check the numbers look right on the HUD), then
briefly nudges each control axis and watches the response to work out the
sign conventions. It saves them to dcs_calibration.json, which DcsF16Env
loads. The nudges are small (0.2 to 0.3 of full stick for one second, then
the opposite to undo) and the jet is handed back after each test.
"""

import argparse
import time

import numpy as np

from dcsai.dcs_bridge import Calibration, DcsLink, to_flight_state


def hold(link, controls, seconds, hz=20):
    """Send the same controls for a while, returning the states seen."""
    states = []
    cal = Calibration(1, 1, 1, 1)  # raw values, no sign flips
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        link.send_controls(*controls)
        t = link.wait_for_telemetry(timeout=2.0)
        states.append((t, to_flight_state(t, 0.5, cal)))
        time.sleep(1.0 / hz)
    return states


def print_telemetry(link, seconds=3.0):
    print("Live telemetry (DCS units converted to ft / kts / deg):")
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        t = link.wait_for_telemetry(newer_than=(link.latest.seq if link.latest else -1), timeout=5.0)
        s = to_flight_state(t, 0.5, Calibration())
        print(f"  alt {s.alt_ft:7.0f} ft  agl {s.agl_ft:7.0f}  ias {s.kias:5.0f} kts  mach {s.mach:4.2f}  "
              f"hdg {np.degrees(s.heading_rad) % 360:5.1f}  pitch {np.degrees(s.pitch_rad):5.1f}  "
              f"bank {np.degrees(s.roll_rad):6.1f}  aoa {s.alpha_deg:5.1f} (raw {t.aoa:.3f})  g {s.nz_g:4.2f}")
        time.sleep(0.5)


def measure(link, axis, amount, settle=1.0, push=1.0):
    """Nudge one axis + then - (to undo); return (states before, states during the + nudge)."""
    neutral = [0.0, 0.0, 0.0, 0.0]
    before = hold(link, neutral, settle)
    pos = list(neutral)
    pos[axis] = amount
    during = hold(link, pos, push)
    neg = list(neutral)
    neg[axis] = -amount
    hold(link, neg, push)
    hold(link, neutral, settle)
    return before, during


def response(before, during, field):
    """Mean change in a FlightState field caused by the nudge."""
    return np.mean([getattr(s, field) for _, s in during]) - np.mean([getattr(s, field) for _, s in before])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="dcs_calibration.json")
    parser.add_argument("--skip-throttle", action="store_true")
    args = parser.parse_args()

    link = DcsLink()
    try:
        print_telemetry(link)
        first = link.latest
        if first.agl_m * 3.28 < 8000:
            raise SystemExit("Climb above 8,000 ft AGL before running the control checks.")

        cal = Calibration()
        q = response(*measure(link, 0, 0.2), "q_rad_s")
        cal.pitch_sign = float(np.sign(q)) or 1.0
        print(f"pitch: +0.2 changed pitch rate {np.degrees(q):+.1f} deg/s -> pitch_sign {cal.pitch_sign:+.0f}")

        p = response(*measure(link, 1, 0.3), "p_rad_s")
        cal.roll_sign = float(np.sign(p)) or 1.0
        print(f"roll:  +0.3 changed roll rate {np.degrees(p):+.1f} deg/s -> roll_sign {cal.roll_sign:+.0f}")

        # The policy's rudder follows JSBSim's convention: + is nose left (yaw rate negative).
        r = response(*measure(link, 2, 0.3), "r_rad_s")
        cal.rudder_sign = -float(np.sign(r)) or 1.0
        print(f"rudder: +0.3 changed yaw rate {np.degrees(r):+.2f} deg/s -> rudder_sign {cal.rudder_sign:+.0f}")

        if not args.skip_throttle:
            hi = hold(link, [0, 0, 0, 0.8], 4.0)
            lo = hold(link, [0, 0, 0, -0.8], 4.0)
            hold(link, [0, 0, 0, 0], 1.0)
            accel_hi = hi[-1][1].kias - hi[0][1].kias
            accel_lo = lo[-1][1].kias - lo[0][1].kias
            cal.throttle_sign = 1.0 if accel_hi > accel_lo else -1.0
            print(f"throttle: +0.8 gave {accel_hi:+.0f} kts, -0.8 gave {accel_lo:+.0f} kts "
                  f"-> throttle_sign {cal.throttle_sign:+.0f}")

        cal.save(args.out)
        print(f"saved {args.out}: {cal}")
        # The + and - nudges don't cancel exactly, so the jet can be left banked.
        # Against fake_dcs it rolled inverted and dove 6,000 ft before the next script took over.
        end = to_flight_state(link.wait_for_telemetry(timeout=2.0), 0.5, cal)
        print(f"Control is yours again. Bank is now {np.degrees(end.roll_rad):+.0f} deg, "
              f"pitch {np.degrees(end.pitch_rad):+.0f} deg: level the jet before running fly_dcs.")
    finally:
        link.close()


if __name__ == "__main__":
    main()
