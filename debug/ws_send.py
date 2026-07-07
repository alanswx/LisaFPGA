import websocket, sys, time
url="ws://192.168.1.196:8182/api/ws"
ws=websocket.create_connection(url, timeout=5)
ws.settimeout(0.5)
# drain greeting
try:
    while True: ws.recv()
except Exception: pass
for msg in sys.argv[1:]:
    ws.send(msg)
    print("SENT:", msg)
    time.sleep(0.15)
time.sleep(0.3)
ws.close()
