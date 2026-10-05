"""Bridge tests against tools.fake_dcs, a JSBSim F-16 speaking the export script's protocol."""

import threading
import time

import numpy as np
import pytest

from dcsai.dcs_bridge import Calibration, DcsF16Env, DcsLink, Telemetry, to_flight_state
from dcsai.spec import Command
from tools import dcs_check
from tools.fake_dcs import FakeDcs

PACKET = b"T,7,12.5,42.1,41.9,4572.0,4500.0,1.5708,0.05,-0.1,195.0,0.72,0.05,0.01,1.02,1.5,0.01,0.02,0.03,1"


def test_parses_telemetry_and_converts_units():
    t = Telemetry.parse(PACKET)
    assert t.seq == 7 and t.ai_in_control
    s = to_flight_state(t, throttle=0.6, cal=Calibration())
    assert s.alt_ft == pytest.approx(15000, abs=1)
    assert s.kias == pytest.approx(379, abs=1)
    assert np.degrees(s.heading_rad) == pytest.approx(90, abs=0.01)
    assert s.alpha_deg == pytest.approx(2.86, abs=0.01)
    assert (s.p_rad_s, s.q_rad_s, s.r_rad_s) == (0.01, 0.03, -0.02)


def test_rejects_malformed_telemetry():
    with pytest.raises(ValueError):
        Telemetry.parse(b"T,1,2,3")
    with pytest.raises(ValueError):
        Telemetry.parse(b"C,1,0,0,0,0")


@pytest.fixture
def fake_dcs():
    def start(signs=(1, 1, 1, -1), seconds=30):
        sim = FakeDcs(signs=signs)
        thread = threading.Thread(target=sim.run, kwargs=dict(seconds=seconds), daemon=True)
        thread.start()
        started.append((sim, thread))
        return sim

    started = []
    yield start
    for sim, thread in started:
        sim.running = False
        thread.join(timeout=2.0)
        sim.close()


def test_policy_actions_reach_the_jet_and_watchdog_releases(fake_dcs):
    fake_dcs(seconds=6)
    link = DcsLink()
    try:
        env = DcsF16Env(link, Command(0, 15000, 380))
        env.reset()
        for _ in range(15):  # 1.5 s of right stick
            _, _, failed, _, info = env.step(np.array([0.0, 0.3, 0.0, 0.5]))
        assert not failed
        assert info["telemetry"].ai_in_control
        assert np.degrees(info["state"].roll_rad) > 10

        time.sleep(0.8)  # stop sending: the watchdog hands control back
        assert not link.wait_for_telemetry(newer_than=link.latest.seq).ai_in_control
    finally:
        link.close()


def test_calibration_detects_a_reversed_pitch_axis(fake_dcs):
    fake_dcs(signs=(-1, 1, 1, -1), seconds=8)
    link = DcsLink()
    try:
        link.wait_for_telemetry()
        q = dcs_check.response(*dcs_check.measure(link, 0, 0.2, settle=0.5, push=1.0), "q_rad_s")
        assert np.sign(q) == -1
    finally:
        link.close()
