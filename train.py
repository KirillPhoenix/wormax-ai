import gymnasium as gym
from stable_baselines3 import PPO
from stable_baselines3.common.callbacks import CheckpointCallback
from stable_baselines3.common.env_util import make_vec_env
from stable_baselines3.common.vec_env import SubprocVecEnv
from wormax_env import WormaxEnv

try:
    print("Запуск обучения PPO")
    env = make_vec_env(
        WormaxEnv,
        n_envs=4,
        env_kwargs={"exe_path": "C:/Users/Phoenix/Documents/GitHub/wormax-ai/wormax.exe"},
        vec_env_cls=SubprocVecEnv
    )
    checkpoint_callback = CheckpointCallback(
        save_freq=10000,
        save_path="./wormax_checkpoints/",
        name_prefix="wormax_ppo"
    )
    model = PPO(
        "CnnPolicy",
        env,
        verbose=1,
        tensorboard_log="./wormax_tensorboard/",
        policy_kwargs={"net_arch": [32, 32]},
        learning_rate=0.0003,
        n_steps=2048,
        batch_size=64,
        n_epochs=10,
    )
    print("Обучение на 100000 шагов...")
    model.learn(
        total_timesteps=100000,
        progress_bar=True,
        callback=checkpoint_callback
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
    env.close()