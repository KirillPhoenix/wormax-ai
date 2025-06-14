import socket
import struct
import time
import random

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', 12346))

def send_packet(dx, dy, boost, stop, ghost):
    data = b''
    data += struct.pack('!f', dx)
    data += struct.pack('!f', dy)
    data += struct.pack('!?', boost)
    data += struct.pack('!?', stop)
    data += struct.pack('!?', ghost)
    sock.sendall(data)
    print(f"Sent raw data: {list(data.hex())}")  # Отладка байтов
    
while True:
    send_packet(round(random.random(), 2), round(random.random(), 2), random.choice([False, True]), False, False)
    time.sleep(2)

