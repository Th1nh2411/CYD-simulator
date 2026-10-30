"""Send serial commands to the running board (no reset) and capture output.

usage: serial_cmd.py PORT SECONDS [command ...]
Commands are sent one per second after a short settle delay.
"""
import serial, sys, time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # Korean text on a cp949 console
port, seconds, cmds = sys.argv[1], float(sys.argv[2]), sys.argv[3:]
s = serial.Serial()
s.port, s.baudrate, s.timeout = port, 115200, 0.1
s.dtr = False  # keep IO0/EN released so opening the port does not reset the ESP32
s.rts = False
s.open()

start = time.time()
next_cmd = start + 1.0
buf = b""
while time.time() - start < seconds:
    if cmds and time.time() >= next_cmd:
        cmd = cmds.pop(0)
        if cmd.startswith("wait:"):  # wait:N pauses N seconds before the next command
            next_cmd = time.time() + float(cmd[5:])
            continue
        s.write((cmd + "\n").encode())
        next_cmd = time.time() + 1.0
    buf += s.read(4096)
s.close()
print(buf.decode("utf-8", errors="replace"))
