from stable_baselines3 import PPO
from wormax_env import WormaxEnv

env = WormaxEnv(exe_path="C:/Users/Phoenix/Documents/GitHub/wormax-ai/wormax.exe")
model = PPO.load("wormax_ppo")
obs, _ = env.reset()
for _ in range(10000):
    action, _ = model.predict(obs)
    obs, reward, done, truncated, _ = env.step(action)
    if done or truncated:
        obs, _ = env.reset()
env.close()