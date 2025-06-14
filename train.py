import gymnasium as gym
from stable_baselines3 import PPO
from stable_baselines3.common.callbacks import CheckpointCallback, BaseCallback
from stable_baselines3.common.env_util import make_vec_env
from stable_baselines3.common.vec_env import SubprocVecEnv
from wormax_env import WormaxEnv

class RewardLoggerCallback(BaseCallback):
    def __init__(self, verbose=0):
        super().__init__(verbose)

    def _on_step(self) -> bool:
        if 'infos' in self.locals:
            for info in self.locals['infos']:
                if isinstance(info, dict) and 'episode' in info:
                    print(f"🏆 Reward: {info['episode']['r']}")
        return True

def make_wormax_env(rank, exe_path, frame_skip):
    def _init():
        return WormaxEnv(env_id=rank, exe_path=exe_path, frame_skip=frame_skip)
    return _init

def main():
    try:
        print("Запуск обучения PPO")

        from stable_baselines3.common.vec_env import SubprocVecEnv

        def make_env(rank):
            def _init():
                return WormaxEnv(
                    env_id=rank,
                    exe_path="C:/Users/Phoenix/Documents/GitHub/wormax-ai/wormax.exe",
                    frame_skip=2
                )
            return _init

        env = SubprocVecEnv([make_env(i) for i in range(2)])

        model = PPO(
            "CnnPolicy",
            env,
            verbose=1,
            tensorboard_log="./wormax_tensorboard/",
            learning_rate=0.0003,
            n_steps=2048,
            batch_size=64,
            n_epochs=10,
        )

        print("Обучение на 100000 шагов...")
        model.learn(
            total_timesteps=100000,
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