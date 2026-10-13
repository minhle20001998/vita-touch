import struct, subprocess, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), "src")
EXE = os.path.join(HERE, "test_gesture")

r = subprocess.run(
    ["g++", "-O2", "-Wall",
     "-I", os.path.join(HERE, "stubs"),
     "-I", SRC,
     os.path.join(HERE, "test_gesture.cpp"),
     os.path.join(HERE, "fakeenv.cpp"),
     os.path.join(SRC, "touchpad.cpp"),
     os.path.join(SRC, "config.cpp"),
     os.path.join(HERE, "fakefs.cpp"),
     "-o", EXE],
    capture_output=True, text=True)
assert r.returncode == 0, r.stderr

r = subprocess.run([EXE], capture_output=True, text=True)
assert r.returncode == 0, r.stderr

HDR = struct.Struct("<4sBBHII16s")

scens, cur = {}, None
for line in r.stdout.strip().split("\n"):
    parts = line.split(" ")
    if parts[0] == "SCEN":
        cur = parts[1]
        scens[cur] = []
    else:
        scens[cur].append(bytes.fromhex(parts[1]))

fails = []
def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg)
    if not cond:
        fails.append(msg)

def events(name):
    out = []
    last_seq = -1
    for d in scens[name]:
        magic, ver, typ, flags, seq, ts, pc = HDR.unpack_from(d, 0)
        assert magic == b"VTPD" and ver == 2, f"{name}: bad header"
        assert pc.rstrip(b"\x00") == b"pw", f"{name}: bad passcode"
        assert seq > last_seq, f"{name}: seq not monotonic"
        last_seq = seq
        body = d[HDR.size:]
        if typ == 0x02:
            dx, dy = struct.unpack("<hh", body)
            out.append(("MOVE", dx, dy))
        elif typ == 0x03:
            f, dx, dy = struct.unpack("<Bhh", body)
            out.append(("FMOVE", f, dx, dy))
        elif typ == 0x04:
            out.append(("BTN", body[0], body[1]))
        elif typ == 0x08:
            out.append(("SWIPE", body[0], body[1]))
        elif typ == 0x09:
            f, dsp = struct.unpack("<Bh", body)
            out.append(("ZOOM", f, dsp))
        elif typ == 0x0A:
            out.append(("END", body[0], body[1]))
        else:
            out.append(("OTHER", typ))
    return out

def move_sum(evs):
    return (sum(e[1] for e in evs if e[0] == "MOVE"),
            sum(e[2] for e in evs if e[0] == "MOVE"))

def fmove_sum(evs, fingers):
    fs = [e for e in evs if e[0] == "FMOVE" and e[1] == fingers]
    return (sum(e[2] for e in fs), sum(e[3] for e in fs))

def btns(evs):
    return [e for e in evs if e[0] == "BTN"]

# 1. move: windowed motion flushes exact, then live; slop still applies first
ev = events("move")
check(ev == [("MOVE", 20, 10), ("MOVE", 20, 20), ("MOVE", 20, 20)],
      "move: flush (20,10) + live (20,20)x2; sub-slop head swallowed")

# 2. tap
ev = events("tap")
check(ev == [("BTN", 1, 1), ("BTN", 1, 0)], "tap: L down then L up")
check(len(ev) == 2, "tap: exactly 2 packets")

# 3. drag
ev = events("drag")
check(ev[0] == ("BTN", 1, 1), "drag: starts with L down")
check(ev[-1] == ("BTN", 1, 0), "drag: ends with L up")
check(move_sum(ev) == (20, 0), "drag: MOVE sums to (20,0)")

# 4. two-finger motion -> FMOVE(2) + END
ev = events("scroll2")
check(ev == [("FMOVE", 2, 0, 40), ("END", 0x03, 2)],
      "scroll2: FMOVE(2) then END, no buttons")

# 5. two-finger tap
ev = events("tap2")
check(ev == [("BTN", 2, 1), ("BTN", 2, 0)], "tap2: R down then R up")

# 6. drag intent aborted by second finger: no premature DOWN (two-stage
# hold), then FMOVE(2) + END; no MOVE
ev = events("abort")
check(ev == [("FMOVE", 2, 20, 0), ("END", 0x03, 2)],
      "abort: motionless hold sends nothing; FMOVE(2), END after")

# 7. scroll, lift one, release: FMOVE + END, no click at all
ev = events("lift21")
check(ev == [("FMOVE", 2, 30, 0), ("END", 0x03, 2)],
      "lift21: FMOVE(2), END on step-down, no stray click")

# 8. scroll mode: one-finger motion -> FMOVE(1) + END, tap still clicks
ev = events("scrollmode")
check(ev == [("FMOVE", 1, 15, 0), ("END", 0x03, 1), ("BTN", 1, 1), ("BTN", 1, 0)],
      "scrollmode: FMOVE(1), END, then L click; no MOVE")

# 9. wobble after 2->1: FMOVE, END on step-down, trembles silent, no click
ev = events("wobble")
check(ev == [("FMOVE", 2, 30, 0), ("END", 0x03, 2)],
      "wobble: FMOVE, END, sub-slop trembles emit nothing, no click")

# 10. real move after 2->1: FMOVE, END, then MOVE; no click
ev = events("lifemove")
check(ev == [("FMOVE", 2, 30, 0), ("END", 0x03, 2), ("MOVE", 20, 0)],
      "lifemove: FMOVE, END, then MOVE; no click")

# 11. dropped frame does not split a tap
ev = events("flicker")
check(ev == [("BTN", 1, 1), ("BTN", 1, 0)], "flicker: exactly one click pair")

# 12. 2->3 join: FMOVE(2), END, FMOVE(3), END; no click on release
ev = events("threejoin")
check(ev == [("FMOVE", 2, 30, 0), ("END", 0x03, 2),
             ("FMOVE", 3, 20, 0), ("END", 0x03, 3)],
      "threejoin: each bout streams then terminates")

# 13. quick cascade + fast release = deliberate 3-tap
ev = events("cascadetap")
check(ev == [("BTN", 3, 1), ("BTN", 3, 0)], "cascadetap: M down then M up")

# 14. stick full deflection
ev = events("stick")
check(move_sum(ev) == (600, 0), "stick: 60 frames at 600u/s sums to (600,0)")
check(len(ev) == 60, "stick: one MOVE per frame")

# 15. stick deadzone
ev = events("stickdead")
check(ev == [], "stickdead: inside deadzone emits nothing")

# 16. stick diagonal normalized
ev = events("stickdiag")
sx, sy = move_sum(ev)
check(abs(sx - 424) <= 2 and abs(sy - 424) <= 2,
      f"stickdiag: diagonal sums to ~(424,424), got ({sx},{sy})")

# 17. stick in scroll mode
ev = events("stickscroll")
check(fmove_sum(ev, 1) == (600, 0), "stickscroll: 60 frames FMOVE(1) sum to (600,0)")
check(move_sum(ev) == (0, 0), "stickscroll: no MOVE in scroll mode")

# 18. clean 3-finger tap
ev = events("threetap")
check(ev == [("BTN", 3, 1), ("BTN", 3, 0)], "threetap: M down then M up")

# 19. slow placement still taps (no premature DOWN anywhere)
ev = events("slowplacetap")
check(ev == [("BTN", 3, 1), ("BTN", 3, 0)],
      "slowplacetap: M down then M up, no L pair")

# 20. hold-then-place: failsafe DOWN once, later fingers silent
ev = events("holdadd")
check(ev == [("BTN", 1, 1), ("BTN", 1, 0)],
      "holdadd: exactly one L pair from the failsafe, then silence")

# 21. staggered 2-tap lift inherits the right click
ev = events("stag2tap")
check(ev == [("BTN", 2, 1), ("BTN", 2, 0)],
      "stag2tap: R down then R up despite staggered lift")

# 22. staggered lift then real move kills the tap
ev = events("stag2move")
check(ev == [("MOVE", 20, 0)], "stag2move: MOVE streams, no click")

# 23. failsafe long hold presses even motionless
ev = events("failsafe")
check(ev == [("BTN", 1, 1), ("MOVE", 20, 0), ("BTN", 1, 0)],
      "failsafe: DOWN at 3x hold, MOVE, UP")

# 30. rapid re-tap: split, never merge/teleport
ev = events("retapAB")
check(ev == [("BTN", 1, 1), ("BTN", 1, 0), ("BTN", 1, 1), ("BTN", 1, 0)],
      "retapAB: two complete clicks, zero MOVE")

# 31. near-spot flicker merges
ev = events("nearflicker")
check(ev == [("BTN", 1, 1), ("BTN", 1, 0)],
      "nearflicker: single click, flicker absorbed")

# 32. gap mid-drag splits cleanly
ev = events("splitdrag")
check(ev == [("BTN", 1, 1), ("MOVE", 50, 0), ("BTN", 1, 0),
             ("BTN", 1, 1), ("BTN", 1, 0)],
      "splitdrag: drag ends, fresh tap begins, no teleport")

# 33. swipe tail step-down: no zoom from the spreading pair
ev = events("swipetail")
check(ev == [("FMOVE", 3, 40, 0)] * 4 + [("END", 0x03, 3), ("SWIPE", 3, 4)],
      "swipetail: stream, END, SWIPE(3,RIGHT); step-down pair never zooms")

# 34. mid-swipe flicker: displacement preserved across dropout, no ZOOM
ev = events("swipeflicker")
check(ev == [("FMOVE", 3, 40, 0), ("END", 0x03, 3)] + [("FMOVE", 3, 40, 0)] * 3 +
      [("END", 0x03, 3), ("SWIPE", 3, 4)],
      "swipeflicker: pre-dropout motion kept, no ZOOM, one SWIPE")

# 24. 3-finger swipe up: stream, END, then one SWIPE
ev = events("threeswipe")
check([e for e in ev if e[0] == "FMOVE"] == [("FMOVE", 3, 0, -40)] * 4,
      "threeswipe: FMOVE(3) stream sums to (0,-160)")
check(ev[-2:] == [("END", 0x03, 3), ("SWIPE", 3, 1)],
      "threeswipe: END then exactly one SWIPE(3,UP)")

# 25. short 3-finger move: stream + END, below swipeDist -> no SWIPE
ev = events("threeslow")
check([e for e in ev if e[0] == "FMOVE"] == [("FMOVE", 3, 0, -30),
      ("FMOVE", 3, 0, -20), ("FMOVE", 3, 0, -20), ("FMOVE", 3, 0, -10)],
      "threeslow: FMOVE(3) sums to (0,-80)")
check(ev[-1] == ("END", 0x03, 3), "threeslow: stream still terminated")
check(not [e for e in ev if e[0] == "SWIPE"], "threeslow: no SWIPE below swipeDist")

# 26. pinch out and back: ZOOM deltas then END
ev = events("pinch")
check(ev == [("ZOOM", 2, 20), ("ZOOM", 2, 20), ("ZOOM", 2, -20), ("END", 0x09, 2)],
      "pinch: ZOOM deltas 20,20,-20, then END; nothing else")

# 35. diagonal pinch with drift: radial dominates, scroll suppressed
ev = events("radialzoom")
check(ev == [("ZOOM", 2, 25)] * 3 + [("END", 0x09, 2)],
      "radialzoom: three ZOOM(25), END; zero FMOVE despite centroid travel")

# 36. grip settle then scroll: blip never locks, scroll streams
ev = events("settlescroll")
check(ev == [("FMOVE", 2, 30, 0)] * 3 + [("END", 0x03, 2)],
      "settlescroll: FMOVE stream only, settle never zooms")

# 37. singleton merge flicker: stream uninterrupted, single END
ev = events("mergeflicker")
check(ev == [("FMOVE", 2, 40, 40)] * 3 + [("END", 0x03, 2)],
      "mergeflicker: one continuous stream, no phase-switch artifacts")

# 38. seeded triple vote: 3-portion alone (96) is below swipeDist(100)
ev = events("seededswipe")
check([e for e in ev if e[0] == "FMOVE"] == [("FMOVE", 3, 24, 24)] * 2,
      "seededswipe: FMOVE(3) stream intact")
check(ev[-2:] == [("END", 0x03, 3), ("SWIPE", 3, 4)],
      "seededswipe: seed carries vote over the line -> SWIPE(3,RIGHT)")

# 39. short swipe (~110 units): fires under the new default 100
ev = events("shortswipe")
check(fmove_sum(ev, 3) == (0, -110), "shortswipe: FMOVE(3) sums to (0,-110)")
check(ev[-2:] == [("END", 0x03, 3), ("SWIPE", 3, 1)],
      "shortswipe: END then SWIPE(3,UP)")

# 40. sub-threshold 3-motion: streams, never swipes
ev = events("sub40")
check(fmove_sum(ev, 3) == (0, -30), "sub40: FMOVE(3) sums to (0,-30)")
check(ev[-1] == ("END", 0x03, 3), "sub40: stream still terminated")
check(not [e for e in ev if e[0] == "SWIPE"], "sub40: no SWIPE below threshold")

# 43. phantom second finger: never confirms, stream untouched
ev = events("phantom2")
check(ev == [("MOVE", 80, 0), ("MOVE", 40, 0), ("MOVE", 40, 0)],
      "phantom2: flush sums head motion, then live; zero FMOVE/buttons/END")

# 27. staggered 3-finger swipe up (lift 3->2->0): swipe survives step-down
ev = events("staggeredswipe")
check([e for e in ev if e[0] == "FMOVE"] == [("FMOVE", 3, 0, -40)] * 4,
      "staggeredswipe: FMOVE(3) stream intact")
check(ev[-2:] == [("END", 0x03, 3), ("SWIPE", 3, 1)],
      "staggeredswipe: END then SWIPE(3,UP) despite staggered lift")

# 28. staggered 3-finger tap: middle click survives step-down
ev = events("staggeredtap")
check(ev == [("BTN", 3, 1), ("BTN", 3, 0)],
      "staggeredtap: M down then M up despite staggered lift")

# 29. 3-finger scroll, step down, release: session swipe fires
ev = events("scrollstepdown")
check([e for e in ev if e[0] == "FMOVE"] == [("FMOVE", 3, 0, 40)] * 4,
      "scrollstepdown: FMOVE(3) stream intact")
check(ev[-2:] == [("END", 0x03, 3), ("SWIPE", 3, 2)],
      "scrollstepdown: END then SWIPE(3,DOWN)")

# 41. sustained merge mid-scroll keeps FMOVE(2) flowing
ev = events("mergescroll")
check(ev == [("FMOVE", 2, 40, 40)] * 6 + [("END", 0x03, 2)],
      "mergescroll: continuous FMOVE(2), single END, zero MOVE")

# 42. merge-hold-release with a clean bout: right click, not silence
ev = events("mergetap")
check(ev == [("BTN", 2, 1), ("BTN", 2, 0)],
      "mergetap: merged pair taps right, no phase switch")

# 43. jump cap absorbs the teleport, stream resumes after
ev = events("jumpcap")
check(ev == [("MOVE", 20, 0)], "jumpcap: 200-unit jump absorbed, MOVE(20,0) after")

# 44. close landing then split: zero MOVE ever
ev = events("holdclose")
check(ev == [("FMOVE", 2, 20, 20), ("END", 0x03, 2)],
      "holdclose: pre-split motion discarded, clean FMOVE + END")

# 45. fast flick: sustained over-cap deltas stream through, travel preserved
ev = events("flick")
check(ev == [("MOVE", 490, 0), ("MOVE", 140, 0), ("MOVE", 140, 0)],
      "flick: over-cap same-direction streak passes, total travel 770 kept")

sys.exit(1 if fails else 0)
