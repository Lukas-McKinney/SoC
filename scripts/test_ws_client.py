#!/usr/bin/env python3
import sys
import socket
import base64
import os
import time

if len(sys.argv) < 2:
    print("Usage: test_ws_client.py ROLE [room] [wait_seconds]")
    sys.exit(2)

role = sys.argv[1].upper()
room = sys.argv[2] if len(sys.argv) > 2 else "testroom"
wait_seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
host = '127.0.0.1'
port = 24680

key = base64.b64encode(os.urandom(16)).decode('ascii')
path = f"/?room={room}&role={role}"
req = (
    f"GET {path} HTTP/1.1\r\n"
    f"Host: {host}:{port}\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    f"Sec-WebSocket-Key: {key}\r\n"
    "\r\n"
)

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(5)
try:
    s.connect((host, port))
except Exception as e:
    print('connect failed:', e)
    sys.exit(1)

s.send(req.encode('utf-8'))
resp = b''
while b'\r\n\r\n' not in resp:
    chunk = s.recv(1024)
    if not chunk:
        print('no response')
        s.close()
        sys.exit(1)
    resp += chunk

print('handshake response:\n', resp.decode('utf-8', errors='replace'))

# send a masked text frame
msg = f"hello from {role}".encode('utf-8')
mask = os.urandom(4)
ln = len(msg)
if ln <= 125:
    header = bytes([0x81, 0x80 | ln])
elif ln <= 0xFFFF:
    header = bytes([0x81, 0xFE]) + ln.to_bytes(2, 'big')
else:
    header = bytes([0x81, 0xFF]) + ln.to_bytes(8, 'big')

frame = bytearray()
frame.extend(header)
frame.extend(mask)
for i,b in enumerate(msg):
    frame.append(b ^ mask[i & 3])

s.send(bytes(frame))
print('sent message')

# keep the connection open briefly so a peer can join and the relay can forward
# traffic back to us.
try:
    deadline = time.time() + wait_seconds
    s.settimeout(1.0)
    while time.time() < deadline:
        try:
            data = s.recv(4096)
        except socket.timeout:
            continue

        if not data:
            print('peer closed connection')
            break

        print('received raw:', data)
        # minimal parse
        b0 = data[0]
        b1 = data[1]
        plen = b1 & 0x7f
        idx = 2
        if plen == 126:
            plen = int.from_bytes(data[idx:idx+2], 'big'); idx += 2
        elif plen == 127:
            plen = int.from_bytes(data[idx:idx+8], 'big'); idx += 8
        mask = None
        if (b1 & 0x80):
            mask = data[idx:idx+4]; idx += 4
        payload = data[idx:idx+plen]
        if mask:
            payload = bytes([payload[i] ^ mask[i&3] for i in range(len(payload))])
        print('payload:', payload)
except Exception as e:
    print('recv error or timeout:', e)

s.close()
