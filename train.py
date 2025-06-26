import gymnasium as gym
from stable_baselines3 import PPO
from stable_baselines3.common.callbacks import CheckpointCallback, BaseCallback
from stable_baselines3.common.env_util import make_vec_env
from stable_baselines3.common.vec_env import SubprocVecEnv
from wormax_env import WormaxEnv
import os
import datetime

class RewardLoggerCallback(BaseCallback):
    def __init__(self, log_path="training_log.txt", verbose=0):
        super().__init__(verbose)
        self.log_path = log_path
        self.episode_count = 0

    def _on_training_start(self) -> None:
        with open(self.log_path, "w") as f:
            f.write(f"Training started at {datetime.datetime.now()}\n\n")

    def _on_step(self) -> bool:
        if 'infos' in self.locals:
            for info in self.locals['infos']:
                if isinstance(info, dict) and 'episode' in info:
                    r = info['episode']['r']
                    l = info['episode']['l']
                    self.episode_count += 1
                    with open(self.log_path, "a") as f:
                        f.write(f"Episode {self.episode_count}: reward = {r}, length = {l}\n")
        return True

    def _on_rollout_end(self) -> None:
        # Записываем метрики после каждой итерации PPO
        logs = self.model.logger.name_to_value
        with open(self.log_path, "a") as f:
            f.write("\nRollout Summary:\n")
            for k, v in logs.items():
                f.write(f"{k}: {v}\n")
            f.write("\n")

def make_wormax_env(rank, exe_path, frame_skip):
    def _init():
        return WormaxEnv(env_id=rank, exe_path=exe_path, frame_skip=frame_skip)
    return _init

def main():
    try:
        print("Запуск обучения PPO")

        env = SubprocVecEnv([make_wormax_env(i, "C:/Users/Phoenix/Documents/GitHub/wormax-ai/wormax.exe", frame_skip=2) for i in range(8)])

        model = PPO(
            "CnnPolicy",
            env,
            verbose=1,
            tensorboard_log="./wormax_tensorboard/",
            learning_rate=2.5e-4,
            n_steps=2048,
            batch_size=1024,
            n_epochs=4,
            gamma=0.99,
            gae_lambda=0.95,
            clip_range=0.2,
            ent_coef=0.01,
            vf_coef=0.5,
        )

        print("Обучение на 500000 шагов...")
        model.learn(
            total_timesteps=500000,
            callback=RewardLoggerCallback(log_path="training_log.txt"),
            progress_bar=True
        )

        print("Сохранение финальной модели...")
        model.save("wormax_ppo_final")
        print("Модель сохранена в wormax_ppo_final.zip")

    except Exception as e:
        print(f"Ошибка обучения: {repr(e)}")
        if 'model' in locals():
            print("Сохраняем модель несмотря на ошибку...")
            model.save("wormax_ppo_error")
            print("Модель сохранена в wormax_ppo_error.zip")
        raise
    finally:
        print("Закрытие среды...")
        if 'env' in locals():
            env.close()

if __name__ == "__main__":
    from multiprocessing import freeze_support
    freeze_support()
    main()
