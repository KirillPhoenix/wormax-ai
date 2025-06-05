import gymnasium as gym
from stable_baselines3 import PPO
from stable_baselines3.common.callbacks import CheckpointCallback
from wormax_env import WormaxEnv
import time

try:
    print("Запуск обучения PPO")
    env = WormaxEnv(exe_path="C:/Users/Phoenix/Documents/GitHub/wormax-ai/wormax.exe")
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
    total_steps = 100000
    steps_per_chunk = 10000
    for i in range(total_steps // steps_per_chunk):
        print(f"Чанк {i+1}/{total_steps//steps_per_chunk}")
        model.learn(
            total_timesteps=steps_per_chunk,
            progress_bar=True,
            callback=checkpoint_callback,
            reset_num_timesteps=False
        )
        model.save(f"wormax_ppo_chunk_{i}")
        print(f"Пауза 5 минут...")
        time.sleep(300)
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