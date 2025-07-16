# train_signal.py

import gymnasium as gym
import numpy as np
import math
import random
import cv2
from collections import deque
from stable_baselines3 import PPO

class WormaxEnvSimple(gym.Env):
    def __init__(self, render_mode=False):
        super().__init__()
        self.render_mode = render_mode
        
        self.view_size = 100
        self.size = 500
        self.radius = self.size // 2
        self.center = np.array([self.radius, self.radius])
        self.speed = 5.0
        self.turn_rate = math.radians(15)
        self.food_count = 20
        self.segment_spacing = 4
        self.initial_length = 10
        self.worm_radius = 5
        self.food_radius = 4
        self.dt = 1.0

        self.action_space = gym.spaces.Discrete(3)  # LEFT, STRAIGHT, RIGHT
        self.observation_space = gym.spaces.Box(low=-1.0, high=1.0, shape=(6,), dtype=np.float32)

        self.reset()

    def reset(self, seed=None, options=None):
        self.head = self._rand_pos()
        self.direction = self._rand_unit()
        self.segments = deque([self.head.copy() for _ in range(self.initial_length)], maxlen=200)
        self.food = [self._rand_pos() for _ in range(self.food_count)]
        self.growth = 0
        self.done = False
        return self._get_obs(), {}

    def step(self, action):
        if action == 0:
            angle = -self.turn_rate
        elif action == 2:
            angle = self.turn_rate
        else:
            angle = 0

        c, s = math.cos(angle), math.sin(angle)
        self.direction = np.dot([[c, -s], [s, c]], self.direction)
        self.direction /= np.linalg.norm(self.direction)

        new_head = self.head + self.direction * self.speed
        if np.linalg.norm(new_head - self.center) > self.radius:
            return self._get_obs(), -2.0, True, False, {}

        self.head = new_head
        self.segments.appendleft(new_head.copy())
        for i in range(1, len(self.segments)):
            d = self.segments[i - 1] - self.segments[i]
            dist = np.linalg.norm(d)
            if dist > self.segment_spacing:
                self.segments[i] = self.segments[i - 1] - d * (self.segment_spacing / dist)

        if self.growth > 0:
            self.growth -= 1
        else:
            self.segments.pop()

        reward = 0.02  # выживание

        nearest = min(self.food, key=lambda f: np.linalg.norm(f - self.head))
        dist_before = np.linalg.norm(self.last_food_vec) if hasattr(self, 'last_food_vec') else 9999
        dist_now = np.linalg.norm(nearest - self.head)
        if dist_now < dist_before:
            reward += 0.1  # приближение
        self.last_food_vec = nearest - self.head

        for i, f in enumerate(self.food):
            if np.linalg.norm(f - self.head) < self.worm_radius + self.food_radius:
                self.food[i] = self._rand_pos()
                self.growth += 10
                reward += 1.0

        return self._get_obs(), reward, False, False, {}

    def _get_obs(self):
        nearest = min(self.food, key=lambda f: np.linalg.norm(f - self.head))
        vec = nearest - self.head
        dist = np.linalg.norm(vec)
        if dist < 1e-5:
            vec = np.zeros(2)
            dist = 1
        dir_to_food = vec / dist
        dist_norm = np.clip(dist / self.radius, 0.0, 1.0)
        dist_to_wall = (self.radius - np.linalg.norm(self.head - self.center)) / self.radius
        obs = np.concatenate([dir_to_food, [dist_norm], self.direction, [dist_to_wall]])
        return obs.astype(np.float32)

    def _rand_pos(self):
        angle = random.uniform(0, 2 * math.pi)
        # Ограничим спавн едой на 90% радиуса
        r = self.radius * math.sqrt(random.uniform(0, 0.81))  # (0.9)^2 = 0.81
        return self.center + r * np.array([math.cos(angle), math.sin(angle)])


    def _rand_unit(self):
        a = random.uniform(0, 2 * math.pi)
        return np.array([math.cos(a), math.sin(a)])

    def render(self):
        # Полная арена
        full = np.zeros((self.size, self.size, 3), dtype=np.uint8)

        # Граница арены
        cv2.circle(
            full,
            center=(int(self.center[0]), int(self.center[1])),
            radius=self.radius,
            color=(100, 100, 255),
            thickness=5
        )

        # Еда
        for f in self.food:
            cv2.circle(full, (int(f[0]), int(f[1])), self.food_radius, (250, 5, ), -1)

        # Червь
        for s in self.segments:
            cv2.circle(full, (int(s[0]), int(s[1])), self.worm_radius, (5, 255, 5), -1)

        # === Камера: вокруг головы ===
        cx, cy = int(self.head[0]), int(self.head[1])
        v = self.view_size
        x1, y1 = max(0, cx - v), max(0, cy - v)
        x2, y2 = min(self.size, cx + v), min(self.size, cy + v)
        view = full[y1:y2, x1:x2]

        # Добавим рамки, если подполз к краю
        top = max(0, 2 * v - view.shape[0])
        left = max(0, 2 * v - view.shape[1])
        if top > 0 or left > 0:
            view = cv2.copyMakeBorder(view, top, 0, left, 0, cv2.BORDER_CONSTANT, value=0)

        # Масштабируем до фиксированного размера (например, 400x400)
        resized = cv2.resize(view, (400, 400), interpolation=cv2.INTER_NEAREST)
        cv2.imshow("Wormax (camera view)", resized)
        cv2.waitKey(1)

# === TRAIN ===
if __name__ == "__main__":
    env = WormaxEnvSimple()
    model = PPO("MlpPolicy", env, verbose=1)
    model.learn(total_timesteps=500000)
    model.save("ppo_wormax_signals")

    # Тест
    test_env = WormaxEnvSimple(render_mode=True)
    obs, _ = test_env.reset()
    for _ in range(1000):
        action, _ = model.predict(obs)
        obs, reward, done, _, _ = test_env.step(action)
        test_env.render()
        if done:
            obs, _ = test_env.reset()
