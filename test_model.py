from stable_baselines3 import PPO
from wormax_env import WormaxEnv
import numpy as np

# Инициализация среды
env = WormaxEnv("wormax.exe")
model = PPO.load("wormax_ppo_final.zip")

# Сброс среды
obs, _ = env.reset()  # Распаковываем, берём только obs
total_reward = 0
food_eaten = 0
steps = 0

# Тест на 10к шагов
for _ in range(10000):
    action, _ = model.predict(obs, deterministic=True)  # Без рандома
    obs, reward, done, info, dict_ = env.step(action)  # Gym API
    total_reward += reward
    if reward >= 20:  # Еда (rewardFood=20 после правок)
        food_eaten += 1
    steps += 1
    print(f"Step: {steps}, Reward: {reward}, Total Reward: {total_reward}, Food: {food_eaten}")
    if done:
        print(f"Эпизод завершён: Награда={total_reward}, Еда={food_eaten}, Шаги={steps}")
        obs, _ = env.reset()  # Сбрасываем
        total_reward = 0
        food_eaten = 0
        steps = 0

env.close()