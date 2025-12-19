# host_send_packets.py
import socket, time

FWD = 25999   # FWDPORT1 printed by your nettest.py / Makefile
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# send four nicely-formed packets matching nettest.c expectations
for i in range(1,5):
    msg = f"packet {i}".encode()
    s.sendto(msg, ("127.0.0.1", FWD))
    print("sent", msg)
    time.sleep(0.5)
s.close()
