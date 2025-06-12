import gymnasium as gym
import numpy as np
import cv2
import pyautogui
import subprocess
import time
import os
import win32gui
import mss
import socket
import struct

class WormaxEnv(gym.Env):
    def __init__(self, exe_path="C:/Users/Phoenix/Documents/GitHub/wormax-ai/wormax.exe", env_id=0):
        super().__init__()
        self.action_space = gym.spaces.Box(
            low=np.array([-1.0, -1.0, 0.0, 0.0, 0.0], dtype=np.float32),
            high=np.array([1.0, 1.0, 1.0, 1.0, 1.0], dtype=np.float32),
            dtype=np.float32
        )
        self.observation_space = gym.spaces.Box(
            low=0, high=255, shape=(64, 64, 1), dtype=np.uint8
        )

        self.exe_path = exe_path
        self.process = None
        self.hwnd = None
        self.window_rect = None
        pyautogui.FAILSAFE = True
        self.last_reward_time = 0
        self.sct = mss.mss()
        self.last_img = None
        self.frame_count = 0
        self.last_state = np.zeros((64, 64, 1), dtype=np.uint8)
        self.env_id = env_id
        self.port = 12345 + env_id
        self.reward_sock = None  # ← создаётся в reset
        print(f"✅ Initialized WormaxEnv with exe_path: {exe_path}, port: {self.port}")


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
            return np.zeros((64, 64, 1), dtype=np.uint8)

        left, top, right, bottom = self.window_rect
        monitor = {"top": top + 50, "left": left + 50, "width": 200, "height": 200}

        if monitor["width"] <= 0 or monitor["height"] <= 0:
            print("Ошибка: Неверные размеры окна")
            return np.zeros((64, 64, 1), dtype=np.uint8)

        img = np.array(self.sct.grab(monitor))[:, :, 0]
        img = cv2.resize(img, (64, 64))

        if self.last_img is not None and np.array_equal(img, self.last_img):
            print("Повторный скриншот, использую кэш")
            return self.last_img[:, :, np.newaxis]

        self.last_img = img
        return img[:, :, np.newaxis]

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

            if os.path.exists("rewards.txt"):
                os.remove("rewards.txt")    
                print("Удалён rewards.txt")

            if not os.path.exists(self.exe_path):
                raise FileNotFoundError(f"wormax.exe не найден по пути: {self.exe_path}")

            print(f"Запускаю {self.exe_path} с портом {self.port}")
            self.process = subprocess.Popen([self.exe_path, str(self.port)])
            time.sleep(2)

            # Ждём окно
            for attempt in range(10):
                self.hwnd = self._find_window()
                if self.hwnd:
                    break
                print(f"⏳ Окно не найдено, попытка {attempt + 1}")
                time.sleep(0.5)

            if not self.hwnd:
                raise RuntimeError("Окно игры не найдено после 10 попыток")

            # Получаем координаты окна
            left, top, right, bottom = win32gui.GetWindowRect(self.hwnd)
            self.window_rect = (left + 8, top + 31, right - 8, bottom - 8)
            print(f"✅ Окно найдено: {self.window_rect}")

            # Подключение к сокету
            self.reward_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            for attempt in range(10):
                try:
                    self.reward_sock.connect(("localhost", self.port))
                    break
                except ConnectionRefusedError:
                    print(f"Порт {self.port} пока не отвечает, попытка {attempt + 1}")
                    time.sleep(0.5)
            else:
                raise RuntimeError(f"Не удалось подключиться к порту {self.port}")

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
            if self.frame_count % 5 != 0:
                return self.last_state, 0.0, False, False, {}
            dx = action[0] * 100
            dy = action[1] * 100
            boost = action[2] > 0.5
            stop = action[3] > 0.5
            ghost = action[4] > 0.5
            left, top, right, bottom = self.window_rect
            center_x = (left + right) // 2
            center_y = (top + bottom) // 2
            pyautogui.moveTo(center_x + dx, center_y + dy)
            if boost:
                pyautogui.keyDown("q")
            else:
                pyautogui.keyUp("q")
            if stop:
                pyautogui.keyDown("w")
            else:
                pyautogui.keyUp("w")
            if ghost:
                pyautogui.press("e")
            time.sleep(0.001)
            state = self._get_screenshot()
            reward = self._get_reward()
            done = False  # временно, пока ты сам не внедришь флаг конца эпизода
            truncated = False
            self.last_state = state
            print("state shape:", state.shape)
            return state, reward, done, truncated, {}
        except Exception as e:
            print(f"Ошибка шага: {e}")
            return np.zeros((64, 64, 1), dtype=np.uint8), 0.0, True, False, {}

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
            pyautogui.keyUp("q")
            pyautogui.keyUp("w")
            if os.path.exists("rewards.txt"):
                os.remove("rewards.txt")
                print("Удалён rewards.txt")
                
            if self.reward_sock:
                self.reward_sock.close()
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