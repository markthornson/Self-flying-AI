import numpy as np
from gymnasium.utils.env_checker import check_env

from dcsai.envs import JsbsimF16Env
from dcsai.spec import Command


def test_passes_gymnasium_checker():
    check_env(JsbsimF16Env(), skip_render_check=True)


def test_trimmed_start_holds_level_flight():
    env = JsbsimF16Env(randomize=False)
    env.reset(seed=0, options=dict(alt_ft=15000, kias=350, heading_deg=90, command=Command(90, 15000, 350)))
    for _ in range(100):  # 10 s holding the trim stick and throttle
        _, reward, term, _, info = env.step(env.prev_action)
    s = info["state"]
    assert not term
    assert abs(s.alt_ft - 15000) < 200
    assert abs(s.kias - 350) < 15
    assert abs(np.degrees(s.roll_rad)) < 2
    assert reward > 0.9


def test_full_forward_stick_ends_in_crash():
    env = JsbsimF16Env(randomize=False)
    env.reset(seed=0, options=dict(alt_ft=8000, kias=400, heading_deg=0))
    for _ in range(600):
        _, reward, term, _, _ = env.step(np.array([-1.0, 0.0, 0.0, 0.0]))
        if term:
            break
    assert term
    assert reward < 0


def test_g_limiter_caps_a_full_aft_stick_pull():
    env = JsbsimF16Env(randomize=False)
    env.reset(seed=0, options=dict(alt_ft=15000, kias=500, heading_deg=0))
    peak = 0.0
    for _ in range(50):  # 5 s of full back stick at 500 kts
        _, _, term, _, info = env.step(np.array([1.0, 0.0, 0.0, 1.0]))
        peak = max(peak, info["state"].nz_g)
        if term:
            break
    assert not term
    assert 4.0 < peak <= 8.5
