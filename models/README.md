# Trained policies

`ppo_f16_viper_g.zip`: PPO pilot trained 5M steps in `JsbsimF16Env` with the
Viper-style g-command stick (commit 65dcc74). In 100 randomised JSBSim
flights: 0 crashes, median heading/altitude/speed error 6.3° / 32 ft / 3.2 kts.
Flies the example route end to end through the DCS bridge against `fake_dcs`.

    python -m tools.fly_dcs models/ppo_f16_viper_g.zip --hold 270 15000 380 --seconds 60
