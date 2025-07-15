# test_signal.py

import time
from stable_baselines3 import PPO
from train_signal import WormaxEnvSimple  # Импорт среды из обучающего файла

env = WormaxEnvSimple(render_mode=True)
model = PPO.load("ppo_wormax_signals")

obs, _ = env.reset()
for _ in range(1000):
    action, _ = model.predict(obs)
    obs, reward, done, _, _ = env.step(action)
    env.render()
    time.sleep(1/60)  # Ограничение по FPS
    if done:
        obs, _ = env.reset()
