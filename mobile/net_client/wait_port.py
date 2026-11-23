#!/usr/bin/env python3
# mobile/net_client/wait_port.py
# รอจนเซิร์ฟเวอร์เทสต์เปิดพอร์ตจริง (แทน sleep 0.5 ที่บางเครื่องช้าไม่พอ)
#   python3 wait_port.py <host> <port> <timeout วินาที>
import socket
import sys
import time

host, port, timeout = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
deadline = time.monotonic() + timeout
while time.monotonic() < deadline:
    with socket.socket() as s:
        s.settimeout(0.25)
        if s.connect_ex((host, port)) == 0:
            print(f"server ready on {host}:{port}")
            sys.exit(0)
    time.sleep(0.1)
print(f"server did not open {host}:{port} within {timeout}s", file=sys.stderr)
sys.exit(1)
