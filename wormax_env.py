import gymnasium as gym
import numpy as np
import cv2
import subprocess
import time
import os
import win32gui
import mss
import socket
import struct

class WormaxEnv(gym.Env):
    def __init__(self, exe_path="C:/Users/Phoenix/Documents/GitHub/wormax-ai/wormax.exe", env_id=0, frame_skip=2):
        super().__init__()
        self.frame_skip = frame_skip
        self.action_space = gym.spaces.Box(
            low=np.array([-1.0, -1.0, 0.0, 0.0, 0.0], dtype=np.float32),
            high=np.array([1.0, 1.0, 1.0, 1.0, 1.0], dtype=np.float32),
            dtype=np.float32
        )
        self.observation_space = gym.spaces.Box(
            low=0, high=255, shape=(84, 84, 3), dtype=np.uint8  # 84x84x3
        )
        self.exe_path = exe_path
        self.process = None
        self.hwnd = None
        self.window_rect = None
        self.sct = mss.mss()
        self.last_img = None
        self.frame_count = 0
        self.last_state = np.zeros((84, 84, 3), dtype=np.uint8)
        self.env_id = env_id
        self.port = 12345 + env_id * 2
        self.control_port = 12346 + env_id * 2
        self.reward_sock = None
        self.control_sock = None
        print(f"✅ Initialized WormaxEnv: reward_port={self.port}, control_port={self.control_port}")

    def _find_window(self):
        def callback(hwnd, hwnds):
            title = win32gui.GetWindowText(hwnd)
            if title.startswith("Wormax Mini with Bots - " + str(self.port)):
                hwnds.append(hwnd)
            return True
        hwnds = []
        win32gui.EnumWindows(callback, hwnds)
        print(f"Found windows: {hwnds}")
        return hwnds[0] if hwnds else None

    def _get_screenshot(self):
        if not self.hwnd:
            print("Ошибка: Нет дескриптора окна")
            return np.zeros((84, 84, 3), dtype=np.uint8)
        left, top, right, bottom = self.window_rect
        monitor = {"top": top + 50, "left": left + 50, "width": 100, "height": 100}
        if monitor["width"] <= 0 or monitor["height"] <= 0:
            print("Ошибка: Неверные размеры окна")
            return np.zeros((84, 84, 3), dtype=np.uint8)
        img = np.array(self.sct.grab(monitor))[:, :, :3]  # RGB
        img = cv2.resize(img, (84, 84))
        if self.last_img is not None and np.array_equal(img, self.last_img):
            print("Повторный скриншот, использую кэш")
            return self.last_img
        self.last_img = img
        return img

    def _get_reward(self):
        data = self.reward_sock.recv(4)
        reward = struct.unpack('f', data)[0]
        return reward

    def reset(self, seed=None):
        print("Сброс среды...")
        try:
            if self.process:
                self.process.terminate()
                self.process.wait(timeout=2)
                print("Процесс завершён")
            if self.reward_sock:
                self.reward_sock.close()
            if self.control_sock:
                self.control_sock.close()
            if not os.path.exists(self.exe_path):
                raise FileNotFoundError(f"wormax.exe не найден: {self.exe_path}")
            print(f"Запускаю {self.exe_path} с портом {self.port}")
            self.process = subprocess.Popen([self.exe_path, str(self.port)])
            time.sleep(1 + self.env_id * 0.5)  # Задержка для уникальности окон
            for attempt in range(10):
                self.hwnd = self._find_window()
                if self.hwnd:
                    break
                print(f"⏳ Окно не найдено, попытка {attempt + 1}")
                time.sleep(0.5)
            if not self.hwnd:
                raise RuntimeError("Окно игры не найдено")
            left, top, right, bottom = win32gui.GetWindowRect(self.hwnd)
            self.window_rect = (left + 8, top + 31, right - 8, bottom - 8)
            print(f"✅ Окно найдено: {self.window_rect}")
            # Подключение к сокетам
            self.reward_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            for attempt in range(10):
                try:
                    self.reward_sock.connect(("localhost", self.port))
                    print(f"Подключено к reward порт {self.port}")
                    break
                except ConnectionRefusedError:
                    print(f"Порт {self.port} не отвечает, попытка {attempt + 1}")
                    time.sleep(0.5)
            else:
                raise RuntimeError(f"Не удалось подключиться к порту {self.port}")
            self.control_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            for attempt in range(10):
                try:
                    self.control_sock.connect(("localhost", self.control_port))
                    print(f"Подключено к control порт {self.control_port}")
                    break
                except ConnectionRefusedError:
                    print(f"Порт {self.control_port} не отвечает, попытка {attempt + 1}")
                    time.sleep(0.5)
            else:
                raise RuntimeError(f"Не удалось подключиться к порту {self.control_port}")
            self.last_img = None
            self.frame_count = 0
            return self._get_screenshot(), {}
        except Exception as e:
            print(f"Ошибка сброса: {e}")
            raise

    def step(self, action):
        print(f"Действие: {action}")
        try:
            self.frame_count += 1
            if self.frame_skip > 1 and self.frame_count % self.frame_skip != 0:
                return self.last_state, 0.0, False, False, {}
            dx = action[0]
            dy = action[1]
            boost = action[2] > 0.5
            stop = action[3] > 0.5
            ghost = action[4] > 0.5
            # Отправка через сокет
            packet = struct.pack("ff???", dx, dy, boost, stop, ghost)
            print(f"Sending to {self.control_port}: dx={dx}, dy={dy}, boost={boost}, stop={stop}, ghost={ghost}")
            try:
                self.control_sock.send(packet)
            except Exception as e:
                print(f"Ошибка отправки: {e}")
                return np.zeros((84, 84, 3), dtype=np.uint8), 0.0, True, False, {}
            state = self._get_screenshot()
            reward = self._get_reward()
            done = reward < -1.0
            truncated = False
            self.last_state = state
            print("state shape:", state.shape)
            return state, reward, done, truncated, {}
        except Exception as e:
            print(f"Ошибка шага: {e}")
            return np.zeros((84, 84, 3), dtype=np.uint8), 0.0, True, False, {}

    def close(self):
        print("Закрытие среды")
        try:
            if self.process:
                self.process.terminate()
                try:
                    self.process.wait(timeout=2)
                    print("Процесс завершён")
                except subprocess.TimeoutExpired:
                    print("Процесс не завершился, принудительное завершение")
                    os.system("taskkill /IM wormax.exe /F")
            if self.reward_sock:
                self.reward_sock.close()
            if self.control_sock:
                self.control_sock.close()
        except Exception as e:
            print(f"Ошибка закрытия: {e}")

if __name__ == "__main__":
    print("Запуск теста WormaxEnv")
    try:
        env = WormaxEnv()
        obs, _ = env.reset()
        for i in range(1000):
            action = np.array([0.1, 0.1, 0.0, 0.0, 0.0], dtype=np.float32)
            obs, reward, done, truncated, _ = env.step(action)
            print(f"Шаг {i}, Награда: {reward}, Done: {done}")
            if done or truncated:
                print("Эпизод завершён")
                obs, _ = env.reset()
        env.close()
    except Exception as e:
        print(f"Ошибка теста: {e}")