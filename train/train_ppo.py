"""Train a PPO pilot in the JSBSim F-16 env.

    python -m train.train_ppo --steps 3000000 --envs 8 --out runs/ppo_f16
"""

import argparse
from pathlib import Path

from stable_baselines3 import PPO
from stable_baselines3.common.callbacks import CheckpointCallback
from stable_baselines3.common.env_util import make_vec_env
from stable_baselines3.common.vec_env import SubprocVecEnv

from dcsai.envs import JsbsimF16Env


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--steps", type=int, default=3_000_000)
    parser.add_argument("--envs", type=int, default=8)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--out", default="runs/ppo_f16")
    parser.add_argument("--resume", help="path to a saved model .zip to keep training")
    args = parser.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    env = make_vec_env(JsbsimF16Env, n_envs=args.envs, seed=args.seed, vec_env_cls=SubprocVecEnv)

    if args.resume:
        model = PPO.load(args.resume, env=env, device="cpu")
    else:
        model = PPO(
            "MlpPolicy",
            env,
            n_steps=1024,
            batch_size=512,
            n_epochs=10,
            learning_rate=3e-4,
            gamma=0.99,
            gae_lambda=0.95,
            clip_range=0.2,
            ent_coef=0.0,
            # Start with a narrower action distribution (std ~0.37) so early
            # exploration does not saturate the stick.
            policy_kwargs=dict(net_arch=[256, 256], log_std_init=-1.0),
            seed=args.seed,
            device="cpu",
            verbose=1,
        )

    checkpoints = CheckpointCallback(save_freq=max(200_000 // args.envs, 1), save_path=str(out), name_prefix="ckpt")
    model.learn(total_timesteps=args.steps, callback=checkpoints, reset_num_timesteps=not args.resume)
    model.save(out / "final")
    print(f"saved {out / 'final.zip'}")


if __name__ == "__main__":
    main()
