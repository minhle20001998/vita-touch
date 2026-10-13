import struct, subprocess, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), "src")
EXE = os.path.join(HERE, "test_protocol")

r = subprocess.run(
    ["g++", "-O2", "-Wall", "-I", SRC,
     os.path.join(HERE, "test_protocol.cpp"), "-o", EXE],
    capture_output=True, text=True)
assert r.returncode == 0, r.stderr

r = subprocess.run([EXE], capture_output=True, text=True)
assert r.returncode == 0, r.stderr

HDR = struct.Struct("<4sBBHII16s")

fails = []
def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg)
    if not cond:
        fails.append(msg)

pkts = {}
for line in r.stdout.strip().split("\n"):
    name, n, hx = line.split(" ")
    pkts[name] = (int(n), bytes.fromhex(hx))

def hdr(name, typ, seq, ts, pc, size):
    n, d = pkts[name]
    check(n == size, f"{name}: total size {n} == {size}")
    magic, ver, t, flags, s, tm, p = HDR.unpack_from(d, 0)
    check(magic == b"VTPD", f"{name}: magic VTPD")
    check(ver == 2, f"{name}: version 2")
    check(t == typ, f"{name}: type 0x{typ:02x}")
    check(flags == 0, f"{name}: flags 0")
    check(s == seq, f"{name}: seq {seq}")
    check(tm == ts, f"{name}: timestamp {ts}")
    check(p == pc, f"{name}: passcode {pc!r}")
    return d[HDR.size:]

b = hdr("HELLO", 0x01, 7, 123456, b"secret" + b"\x00" * 10, 34)
check(b == bytes([0, 1]), "HELLO payload mode=0 flags=1")

b = hdr("MOVE", 0x02, 8, 123500, b"secret" + b"\x00" * 10, 36)
check(struct.unpack("<hh", b) == (-300, 1024), "MOVE payload dx=-300 dy=1024")

b = hdr("BUTTON", 0x04, 10, 123700, b"secret" + b"\x00" * 10, 34)
check(tuple(b) == (1, 1), "BUTTON payload left down")

b = hdr("FMOVE", 0x03, 9, 123600, b"secret" + b"\x00" * 10, 37)
check(struct.unpack("<Bhh", b) == (2, 0, -120), "FMOVE payload fingers=2 dx=0 dy=-120")

b = hdr("SWIPE", 0x08, 16, 124300, b"secret" + b"\x00" * 10, 34)
check(tuple(b) == (3, 3), "SWIPE payload fingers=3 dir=LEFT")

b = hdr("ZOOM", 0x09, 17, 124400, b"secret" + b"\x00" * 10, 35)
check(struct.unpack("<Bh", b) == (2, -60), "ZOOM payload fingers=2 dSpread=-60")

b = hdr("MODE", 0x05, 11, 123800, b"secret" + b"\x00" * 10, 33)
check(tuple(b) == (1,), "MODE payload scroll")

b = hdr("PING", 0x06, 12, 123900, b"secret" + b"\x00" * 10, 32)
check(b == b"", "PING has no payload")

b = hdr("HELLO_LONGPC", 0x01, 13, 124000, b"0123456789abcdef", 34)
check(True, "long passcode truncated to 16 bytes")

b = hdr("PING_EMPTYPC", 0x06, 14, 124100, b"\x00" * 16, 32)
check(True, "empty passcode zero-fills field")

b = hdr("VPAD", 0x07, 15, 124200, b"secret" + b"\x00" * 10, 34)
check(tuple(b) == (4, 1), "VPAD payload triangle down")

b = hdr("END", 0x0A, 18, 124500, b"secret" + b"\x00" * 10, 34)
check(tuple(b) == (0x03, 2), "END payload ended_type=FMOVE fingers=2")

sys.exit(1 if fails else 0)
