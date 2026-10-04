import numpy as np
import pytest

from dcsai.guidance import NM_FT, RouteFollower, Waypoint, bearing_and_distance, offset_position
from dcsai.spec import OBS_DIM, Command, FlightState, build_observation, heading_error_rad, is_failed, tracking_reward


def level_state(**overrides):
    values = dict(
        roll_rad=0.0, pitch_rad=0.02, heading_rad=np.radians(90), p_rad_s=0.0, q_rad_s=0.0, r_rad_s=0.0,
        kias=350.0, mach=0.7, alpha_deg=2.0, beta_deg=0.0, nz_g=1.0, vs_fps=0.0,
        alt_ft=15000.0, agl_ft=15000.0, throttle=0.5,
    )
    values.update(overrides)
    return FlightState(**values)


def test_heading_error_wraps_through_north():
    state = level_state(heading_rad=np.radians(350))
    assert np.degrees(heading_error_rad(state, Command(10, 15000, 350))) == pytest.approx(20)
    assert np.degrees(heading_error_rad(state, Command(330, 15000, 350))) == pytest.approx(-20)


def test_observation_shape_and_range():
    obs = build_observation(level_state(), Command(270, 30000, 500), np.zeros(4))
    assert obs.shape == (OBS_DIM,)
    assert obs.dtype == np.float32
    assert np.all(np.abs(obs) <= 5.0)


def test_reward_is_highest_on_target():
    on = tracking_reward(level_state(), Command(90, 15000, 350))
    off = tracking_reward(level_state(), Command(180, 18000, 300))
    assert on == pytest.approx(1.0)
    assert off < 0.2


def test_failure_conditions():
    assert not is_failed(level_state())
    assert is_failed(level_state(agl_ft=500))
    assert is_failed(level_state(alpha_deg=35))
    assert is_failed(level_state(nz_g=10))
    assert is_failed(level_state(kias=float("nan")))


def test_offset_then_bearing_round_trips():
    lat, lon = offset_position(42.0, 42.0, 135.0, 20 * NM_FT)
    bearing, dist = bearing_and_distance(42.0, 42.0, lat, lon)
    assert bearing == pytest.approx(135.0, abs=0.1)
    assert dist == pytest.approx(20 * NM_FT, rel=1e-6)


def test_route_follower_advances_on_capture():
    wp1_lat, wp1_lon = offset_position(42.0, 42.0, 0, 10 * NM_FT)
    wp2_lat, wp2_lon = offset_position(wp1_lat, wp1_lon, 90, 10 * NM_FT)
    follower = RouteFollower([Waypoint(wp1_lat, wp1_lon, 15000, 400), Waypoint(wp2_lat, wp2_lon, 12000, 350)])

    cmd = follower.update(42.0, 42.0)
    assert cmd.heading_deg == pytest.approx(0, abs=0.1) and cmd.alt_ft == 15000

    cmd = follower.update(wp1_lat, wp1_lon)  # at WP1, so steer to WP2
    assert follower.index == 1
    assert cmd.heading_deg == pytest.approx(90, abs=0.5) and cmd.alt_ft == 12000

    assert follower.update(wp2_lat, wp2_lon) is None
    assert follower.done
