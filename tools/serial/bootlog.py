"""Reset the board and print its serial output (boot log + first stats).

usage: python tools/serial/bootlog.py [PORT] [SECONDS]     (default COM3, 6 s)
"""
import serial, time, sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # Korean text on a cp949 console
port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 6
s = serial.Serial(port, 115200, timeout=0.2)
# Reset into normal boot: IO0 high (DTR=False), pulse EN via RTS
s.dtr = False
s.rts = True
time.sleep(0.1)
s.rts = False
end = time.time() + seconds
buf = b""
while time.time() < end:
    buf += s.read(4096)
s.close()
print(buf.decode("utf-8", errors="replace"))
