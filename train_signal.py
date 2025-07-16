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

        # Arena parameters
        self.size = 500
        self.radius = self.size // 2
        self.center = np.array([self.radius, self.radius])

        # Worm parameters
        self.speed = 5.0
        self.turn_rate = math.radians(15)
        self.initial_length = 10
        self.segment_spacing = 4
        self.worm_radius = 5

        # Food parameters
        self.food_count = 20
        self.food_radius = 4

        # Observation / action spaces
        self.view_size = 100
        self.action_space = gym.spaces.Discrete(3)  # LEFT, STRAIGHT, RIGHT
        self.observation_space = gym.spaces.Box(
            low=-1.0, high=1.0, shape=(6,), dtype=np.float32
        )

        # Bot parameters
        self.bot_count = 5
        self.bots = []

        self.reset()

    def reset(self, seed=None, options=None):
        # Player
        self.head = self._rand_pos()
        self.direction = self._rand_unit()
        self.segments = deque(
            [self.head.copy() for _ in range(self.initial_length)], 
            maxlen=200
        )
        self.growth = 0

        # Food
        self.food = [self._rand_pos() for _ in range(self.food_count)]
        self.last_food_vec = None

        # Bots
        self.bots = [self._create_bot() for _ in range(self.bot_count)]

        self.done = False
        return self._get_obs(), {}

    def _create_bot(self):
        head = self._rand_pos()
        direction = self._rand_unit()
        segments = deque(
            [head.copy() for _ in range(self.initial_length)],
            maxlen=200
        )
        return {"segments": segments, "direction": direction}

    def step(self, action):
        # ------ PLAYER MOVE ------
        # Turn
        if action == 0:
            angle = -self.turn_rate
        elif action == 2:
            angle = self.turn_rate
        else:
            angle = 0
        c, s = math.cos(angle), math.sin(angle)
        self.direction = np.dot([[c, -s], [s, c]], self.direction)
        self.direction /= np.linalg.norm(self.direction)

        # Advance head
        new_head = self.head + self.direction * self.speed
        if np.linalg.norm(new_head - self.center) > self.radius:
            # Out of bounds → done
            return self._get_obs(), -2.0, True, False, {}
        self.head = new_head

        # Update player's segments
        self.segments.appendleft(self.head.copy())
        for i in range(1, len(self.segments)):
            d = self.segments[i-1] - self.segments[i]
            dist = np.linalg.norm(d)
            if dist > self.segment_spacing:
                self.segments[i] = self.segments[i-1] - d * (self.segment_spacing / dist)
        if self.growth > 0:
            self.growth -= 1
        else:
            self.segments.pop()

        # ------ REWARDS & FOOD ------
        reward = 0.02  # for staying alive

        # Approach food
        nearest = min(self.food, key=lambda f: np.linalg.norm(f - self.head))
        dist_before = np.linalg.norm(self.last_food_vec) if self.last_food_vec is not None else 9999
        dist_now = np.linalg.norm(nearest - self.head)
        if dist_now < dist_before:
            reward += 0.1
        self.last_food_vec = nearest - self.head

        # Eat food
        for i, f in enumerate(self.food):
            if np.linalg.norm(f - self.head) < self.worm_radius + self.food_radius:
                self.food[i] = self._rand_pos()
                self.growth += 10
                reward += 1.0

        # ------ BOTS MOVE & AVOID PLAYER ------
        for bot in self.bots:
            bot_head = bot["segments"][0]
            bot_dir = bot["direction"]

            # Compute angle from bot to player
            to_player = self.head - bot_head
            angle_to_player = math.atan2(to_player[1], to_player[0])
            current_angle = math.atan2(bot_dir[1], bot_dir[0])
            diff = (angle_to_player - current_angle + math.pi) % (2*math.pi) - math.pi

            # Avoid by turning opposite direction
            if diff > 0.1:
                avoid_angle = -self.turn_rate
            elif diff < -0.1:
                avoid_angle = self.turn_rate
            else:
                avoid_angle = 0

            c, s = math.cos(avoid_angle), math.sin(avoid_angle)
            new_dir = np.dot([[c, -s], [s, c]], bot_dir)
            new_dir /= np.linalg.norm(new_dir)
            bot["direction"] = new_dir

            # Advance bot head
            new_bot_head = bot_head + new_dir * self.speed

            # Respawn if OOB
            if np.linalg.norm(new_bot_head - self.center) > self.radius:
                bot.update(self._create_bot())
                continue
            else:
                bot["segments"].appendleft(new_bot_head.copy())

            for seg in self.segments:
                if np.linalg.norm(new_bot_head - seg) < 2 * self.worm_radius:
                    reward += 2.0
                    bot.update(self._create_bot())
                    break

            # Maintain spacing
            segs = bot["segments"]
            for i in range(1, len(segs)):
                d = segs[i-1] - segs[i]
                dist = np.linalg.norm(d)
                if dist > self.segment_spacing:
                    segs[i] = segs[i-1] - d * (self.segment_spacing / dist)
            segs.pop()

            # Collision with player
            if np.linalg.norm(new_bot_head - self.head) < 2 * self.worm_radius:
                reward += 2.0
                bot.update(self._create_bot())

        return self._get_obs(), reward, False, False, {}

    def _get_obs(self):
        # Direction to nearest food + distance + own direction + dist to wall
        nearest = min(self.food, key=lambda f: np.linalg.norm(f - self.head))
        vec = nearest - self.head
        dist = np.linalg.norm(vec)
        if dist < 1e-5:
            vec = np.zeros(2); dist = 1
        dir_to_food = vec / dist
        dist_norm = np.clip(dist / self.radius, 0.0, 1.0)
        dist_to_wall = (self.radius - np.linalg.norm(self.head - self.center)) / self.radius
        obs = np.concatenate([dir_to_food, [dist_norm], self.direction, [dist_to_wall]])
        return obs.astype(np.float32)

    def _rand_pos(self):
        angle = random.uniform(0, 2*math.pi)
        r = self.radius * math.sqrt(random.uniform(0, 0.81))
        return self.center + r * np.array([math.cos(angle), math.sin(angle)])

    def _rand_unit(self):
        a = random.uniform(0, 2*math.pi)
        return np.array([math.cos(a), math.sin(a)])

    def render(self):
        full = np.zeros((self.size, self.size, 3), dtype=np.uint8)
        # Arena boundary
        cv2.circle(
            full, (int(self.center[0]), int(self.center[1])),
            self.radius, (100, 100, 255), thickness=5
        )
        # Food
        for f in self.food:
            cv2.circle(full, (int(f[0]), int(f[1])), self.food_radius, (0, 250, 5), -1)
        # Player
        for s in self.segments:
            cv2.circle(full, (int(s[0]), int(s[1])), self.worm_radius, (5, 255, 5), -1)
        # Bots
        for bot in self.bots:
            for seg in bot["segments"]:
                cv2.circle(full, (int(seg[0]), int(seg[1])), self.worm_radius, (255, 150, 0), -1)

        # Camera view around player
        cx, cy = int(self.head[0]), int(self.head[1])
        v = self.view_size
        x1, y1 = max(0, cx-v), max(0, cy-v)
        x2, y2 = min(self.size, cx+v), min(self.size, cy+v)
        view = full[y1:y2, x1:x2]
        top = max(0, 2*v - view.shape[0])
        left = max(0, 2*v - view.shape[1])
        if top > 0 or left > 0:
            view = cv2.copyMakeBorder(view, top, 0, left, 0, cv2.BORDER_CONSTANT, value=0)
        resized = cv2.resize(view, (400, 400), interpolation=cv2.INTER_NEAREST)
        cv2.imshow("Wormax (camera view)", resized)
        cv2.waitKey(1)

# === TRAIN & TEST ===
if __name__ == "__main__":
    # Train
    env = WormaxEnvSimple()
    model = PPO("MlpPolicy", env, verbose=1)
    model.learn(total_timesteps=100000)
    model.save("ppo_wormax_signals")

    # Test
    test_env = WormaxEnvSimple(render_mode=True)
    obs, _ = test_env.reset()
    for _ in range(1_000):
        action, _ = model.predict(obs)
        obs, reward, done, _, _ = test_env.step(action)
        test_env.render()
        if done:
            obs, _ = test_env.reset()