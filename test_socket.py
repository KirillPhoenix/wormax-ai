import math
import random
import time
import struct
import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', 12346))

def send_packet(angle_degrees, boost, stop, ghost):
    angle_radians = angle_degrees * 3.14159 / 180
    dx = round(math.cos(angle_radians), 4)
    dy = round(math.sin(angle_radians), 4)
    data = b''
    data += struct.pack('!f', dx)
    data += struct.pack('!f', dy)
    data += struct.pack('!?', boost)
    data += struct.pack('!?', stop)
    data += struct.pack('!?', ghost)
    sock.sendall(data)
    print(f"Sent angle: {angle_degrees}, raw dx/dy: {dx}, {dy}")

while True:
    angle = random.randint(0, 359)
    send_packet(angle, random.choice([True, False]), False, False)
    time.sleep(0.5)
