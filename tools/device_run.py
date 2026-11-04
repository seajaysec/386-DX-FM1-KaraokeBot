#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay a simulator script (tests/scenarios/*.sim) on a real FM-1 over USB MIDI, check its `expect`
lines against what the device reports (companion commands 2, 3, 32, 49, 53), and record its USB audio.

    tools/device_run.py tests/scenarios/write.sim [--port NAME] [--wav out.wav] [--audio "386DX FM1"]

The buttons and keys are held from here (command 2, resent often: the FM-1 lets go of them 2 s after
the last message), the encoders turned with command 3. `expect` kinds the device can answer are
checked; the others are listed as skipped. Exit status 1 when an expect fails."""
import argparse, subprocess, sys, time, signal
from pathlib import Path
import mido

BTN = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"]
ENC = ["SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"]
PAGES = ["Sing", "Song", "Phrase", "Sound", "Envelope", "Karaoke", "Mixer", "Setup", "Arp"]
TRACKS = ["Voice", "Keys", "Bass", "Drums"]
TEXT_KEYS = "lambncopdqersftguhvwixjyzk "
HEAD = [0x7D, 0x4F, 0x4D]
# song_t's layout (firmware/src/app/song.h, version 8): where the fields peeked at are
SONG = dict(lane_bars=122, line=126, note=4478, row=7550, lane=7934, pattern=8446)


def le7(n, k):
    return [(n >> (7 * i)) & 127 for i in range(k)]


class Device:
    def __init__(self, port):
        self.i, self.o = mido.open_input(port), mido.open_output(port)
        self.buttons = self.keys = 0

    def close(self):                           # (closed here: left to the garbage collector, a port can close
        for p in (self.i, self.o):               # itself from inside its own MIDI callback and hang the run)
            try:
                p.close()
            except BaseException:
                pass

    def send(self, *b):
        self.o.send(mido.Message("sysex", data=HEAD + list(b)))

    def hold(self):
        self.send(2, *le7(self.buttons, 2), *le7(self.keys, 4))

    def turn(self, e, d):
        while d:
            step = max(-60, min(60, d))
            self.send(3, e, step + 64)
            d -= step

    def ask(self, cmd, *body, wait=1.0):
        t = time.time()
        while self.i.poll() and time.time() - t < 0.05:
            pass                                   # (what came before; bounded: a playing FM-1 never stops sending)
        self.send(cmd, *body)
        t = time.time()
        while time.time() - t < wait:
            m = self.i.poll()
            if m and m.type == "sysex" and list(m.data[:3]) == HEAD and m.data[3] == cmd:
                return list(m.data[4:])
            time.sleep(0.002)
        return None

    def status(self):
        d = self.ask(32)
        if not d:
            return None
        return dict(playing=d[6], page=d[7], track=d[8], slot=-1 if d[9] == 127 else d[9], unsaved=d[10],
                    bpm=d[11] | d[12] << 7, tick=d[16] | d[17] << 7, name=bytes(d[18:42]).split(b"\0")[0].decode(),
                    build=bytes(d[42:65]).split(b"\0")[0].decode(), poetry=d[65] if len(d) > 65 else 0)

    def screen(self):
        d = self.ask(53)
        if not d:
            return None
        f = d[1]
        return dict(page=d[0], list=f & 3, words=f >> 2 & 1, ask=f >> 3 & 1, moving=f >> 4 & 1,
                    bar=d[72], mutes=d[73], misses=d[74] | d[75] << 7, draw_ms=(d[76] | d[77] << 7) if len(d) > 77 else -1, gap_ms=(d[78] | d[79] << 7) if len(d) > 79 else -1, rows=d[2], row=d[3], notes=d[4] | d[5] << 7, cpu=d[6], level=d[7],
                    msg=(bytes(d[8:24]).split(b"\0")[0] + b" " + bytes(d[24:40]).split(b"\0")[0]).decode().strip(), line=bytes(d[40:72]).split(b"\0")[0].decode())

    def peek(self, at, n):
        """n bytes of the song in the FM-1's memory from offset `at` (companion command 43, 32 at a time)"""
        out = []
        while len(out) < n:
            r = self.ask(43, at & 127, at >> 7 & 127, wait=2.0)
            if not r or r[0] or not r[3]:
                return None
            got = [r[4 + i * 2] | r[5 + i * 2] << 7 for i in range(r[3])]
            out += got
            at += len(got)
        return bytes(out[:n])

    def slots(self):
        out = []
        for i in range(8):
            d = self.ask(49, i)
            out.append((d[3], bytes(d[4:28]).split(b"\0")[0].decode()) if d else (0, "?"))
        return out


def song_pitch(dev, row, note):
    """the pitch of note `note` of the line row `row` sings, read from the device (-1: none)"""
    u16 = lambda b: b[0] | b[1] << 8
    r = dev.peek(SONG["row"] + row * 4, 3)
    if not r:
        return None
    line = r[2]
    h = dev.peek(SONG["line"] + line * 68 + 64, 3)
    first, count = u16(h), h[2]
    if note >= count:
        return -1
    return dev.peek(SONG["note"] + (first + note) * 6 + 3, 1)[0]


def song_step(dev, bar, track, s, nudge=False):
    """a step of bar `bar` of the track's chain, as the simulator's `expect step` says it: Drums the drum bits,
    Keys and Bass the first note (0: a rest); or its nudge"""
    m = dev.peek(SONG["lane"] + track * 128 + bar, 1)[0] & 31
    if m >= 16:
        return 0                                       # a bar of rest
    b = dev.peek(SONG["pattern"] + m * 480 + ((track - 1) * 16 + s) * 10, 10)
    if nudge:
        return b[9] - 256 if b[9] > 127 else b[9]
    return (b[0] | b[1] << 8) if track == 3 else (b[2] if b[6] else 0)


def song_phrases(dev, track):
    """the bars of each phrase of the track's chain"""
    n = dev.peek(SONG["lane_bars"] + track, 1)[0]
    lane = dev.peek(SONG["lane"] + track * 128, n)
    starts = [b for b in range(n) if lane[b] & 0x80] + [n]
    return [starts[i + 1] - starts[i] for i in range(len(starts) - 1)]


def find_port(name):
    ports = [p for p in mido.get_input_names() if (name.lower() in p.lower() if name else "fm-1" in p.lower() or "melodee" in p.lower())]
    if not ports:
        raise SystemExit(f"no FM-1 MIDI port ({mido.get_input_names()})")
    return ports[0]


def start_recording(audio, wav):
    out = subprocess.run(["ffmpeg", "-hide_banner", "-f", "avfoundation", "-list_devices", "true", "-i", ""], capture_output=True, text=True).stderr
    idx = None
    audio_part = out.split("audio devices:")[-1]
    for line in audio_part.splitlines():
        if audio.lower() in line.lower() and "] [" in line.replace("[AVFoundation", "[AVFoundation"):
            idx = line.split("] [")[1].split("]")[0] if "] [" in line else None
    if idx is None:
        for line in audio_part.splitlines():
            if audio.lower() in line.lower():
                idx = line[line.rindex("[") + 1:line.rindex("]")]
    if idx is None:
        print(f"audio device {audio!r} not found; not recording")
        return None
    print(f"recording audio device {idx} -> {wav}")
    return subprocess.Popen(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "avfoundation", "-i", f":{idx}", "-ac", "2", "-ar", "44100", str(wav)],
                            stdin=subprocess.DEVNULL)


def stop_recording(p):
    if not p:
        return
    p.send_signal(signal.SIGINT)
    try:
        p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("script", type=Path)
    ap.add_argument("--port")
    ap.add_argument("--wav", type=Path)
    ap.add_argument("--audio", default="386DX FM-1")
    ap.add_argument("--reset", action="store_true", help="empty S2..S8, restart, open Greensleeves and reset every knob first, so the run starts as the simulator does")
    ap.add_argument("--hold", type=int, help="hold every key and button this many ms (a quick real tap is 30-100)")
    ap.add_argument("-v", "--verbose", action="store_true", help="print each line as it runs")
    ap.add_argument("--speed", type=float, default=1.0, help="stretch every pause (the device is slower than the simulator)")
    a = ap.parse_args()
    dev = Device(find_port(a.port))
    st = dev.status()
    if not st:
        raise SystemExit("the FM-1 does not answer as 386DX")
    if a.reset:
        dev.ask(47)
        for slot in range(1, 8):
            dev.ask(51, slot, wait=3.0)
        dev.send(52)
        time.sleep(1.0)
        for _ in range(40):
            time.sleep(0.5)
            try:
                dev.close()
                dev = Device(find_port(a.port))
                if dev.status():
                    break
            except BaseException:
                pass
        st = dev.status()
        # as the simulator starts: Greensleeves loaded from S1
        def press(name, ms=150):
            dev.buttons |= 1 << BTN.index(name)
            end = time.time() + ms / 1000
            while time.time() < end:
                dev.hold(); time.sleep(0.03)
            dev.buttons &= ~(1 << BTN.index(name))
            dev.hold(); time.sleep(0.35)
        if st["name"] != "Greensleeves":
            press("SAVE"); press("SEL")                         # SAVE's list, LOAD: the slots, from the song's own
            dev.turn(ENC.index("SELECT"), -max(st["slot"], 0)); time.sleep(0.4)   # (the list comes round: to S1)
            press("SEL"); time.sleep(1.0)
            press("HOME") if dev.screen()["page"] != 0 else None
            st = dev.status()
        # every knob as a new song has it (GLO's list, RESET KNOBS, yes), whatever was turned by hand before
        press("GLO"); press("SEL"); press("SEL"); press("SEL"); time.sleep(0.5)
        press("HOME")
        if dev.screen()["page"] != 0:
            press("HOME")
        st = dev.status()
    print(f"{st['name']!r} build {st['build']} page {PAGES[st['page']]}")
    rec = start_recording(a.audio, a.wav) if a.wav else None
    time.sleep(1.0)
    fails, skipped, level_seen = 0, set(), 0

    def run(ms):
        nonlocal level_seen
        end = time.time() + ms * a.speed / 1000.0
        while True:
            dev.hold()
            time.sleep(0.04)
            if time.time() >= end:
                break
        s = dev.screen()
        if s:
            level_seen = max(level_seen, s["level"])

    def fail(ln, text):
        nonlocal fails
        fails += 1
        print(f"  FAIL line {ln}: {text}")

    try:
        for ln, raw in enumerate(a.script.read_text().splitlines(), 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if a.verbose:
                print(f"  {ln}: {line}", flush=True)
            cmd, _, rest = line.partition(" ")
            arg, _, more = rest.partition(" ")
            if cmd in ("tap", "down", "up"):
                k = BTN.index(arg)
                if cmd != "up":
                    dev.buttons |= 1 << k
                    run(a.hold or (int(more) if more else 120))
                if cmd != "down":
                    dev.buttons &= ~(1 << k)
                    run(250)               # a person's pause: the device finishes with the press first
                    if arg == "SAVE":
                        run(2500)          # a song takes a moment to write; buttons pressed meanwhile are not seen
            elif cmd in ("key", "keydown", "keyup"):
                k = int(arg)
                if cmd != "keyup":
                    dev.keys |= 1 << k
                    run(a.hold or ((int(more) if more else 150) if cmd == "key" else 60))
                if cmd != "keydown":
                    dev.keys &= ~(1 << k)
                    run(200)
            elif cmd == "turn":
                dev.turn(ENC.index(arg), int(more))
                run(300)
            elif cmd == "type":
                for ch in rest.lower():
                    if ch in TEXT_KEYS:
                        k = TEXT_KEYS.index(ch)
                        dev.keys |= 1 << k
                        run(a.hold or 70)
                        dev.keys &= ~(1 << k)
                        run(70)
            elif cmd == "solo":
                want = TRACKS.index(arg)
                for t in range(4):                      # HOME + black key 1-4 mutes and unmutes
                    muted = (dev.screen()["mutes"] >> t) & 1
                    if muted != (t != want):
                        dev.buttons |= 1 << BTN.index("HOME")
                        run(150)
                        dev.keys |= 1 << (1, 3, 5, 8)[t]
                        run(120)
                        dev.keys &= ~(1 << (1, 3, 5, 8)[t])
                        run(150)
                        dev.buttons &= ~(1 << BTN.index("HOME"))
                        run(250)
            elif cmd == "wait":
                run(int(arg))
            elif cmd == "waittick":
                # until the song plays sixteenth N (the status's playhead), as the simulator's waittick
                want, end = int(arg), time.time() + 120
                while time.time() < end:
                    st = dev.status()
                    if st and st["playing"] and st["tick"] >= want:
                        break
                    run(20)
            elif cmd == "expect":
                s, v = dev.screen(), dev.status()
                if not s or not v:
                    fail(ln, "the FM-1 stopped answering")
                    continue
                if arg == "mode":
                    if v["poetry"] != (more.strip() == "slamjam"):
                        fail(ln, f"mode {'slamjam' if v['poetry'] else 'karaoke'}, expected {more.strip()}")
                    continue
                if arg == "page":
                    if PAGES[s["page"]] != more:
                        fail(ln, f"page {PAGES[s['page']]}, expected {more}")
                elif arg == "track":
                    if TRACKS[v["track"]] != more:
                        fail(ln, f"track {TRACKS[v['track']]}, expected {more}")
                elif arg == "rows":
                    if s["rows"] != int(more):
                        fail(ln, f"{s['rows']} rows, expected {more}")
                elif arg == "notes":
                    if s["notes"] != int(more):
                        fail(ln, f"{s['notes']} notes, expected {more}")
                elif arg == "playing":
                    if v["playing"] != int(more):
                        fail(ln, f"playing {v['playing']}, expected {more}")
                elif arg == "bpm":
                    if v["bpm"] != int(more):
                        fail(ln, f"bpm {v['bpm']}, expected {more}")
                elif arg == "slot":
                    if v["slot"] != int(more):
                        fail(ln, f"slot {v['slot']}, expected {more}")
                elif arg == "dirty":
                    if v["unsaved"] != int(more):
                        fail(ln, f"unsaved {v['unsaved']}, expected {more}")
                elif arg == "msg":
                    got = s["msg"].upper()           # the readback carries 16 characters of the first line
                    if more.upper() not in got and not (len(got) >= 15 and more.upper().startswith(got)):
                        fail(ln, f"message {s['msg']!r}, expected {more!r}")
                elif arg == "line_text":
                    r, _, want = more.partition(" ")
                    if int(r) == s["row"] and s["line"] != want:
                        fail(ln, f"line {s['line']!r}, expected {want!r}")
                elif arg == "misses":
                    if s["misses"] > int(more):
                        fail(ln, f"{s['misses']} voice misses, at most {more}")
                elif arg == "pitch":
                    r, i, want = (int(x) for x in more.split())
                    got = song_pitch(dev, r, i)
                    if got != want:
                        fail(ln, f"row {r} note {i} pitch {got}, expected {want}")
                elif arg == "step":
                    tn, st, want = more.split()
                    t = TRACKS.index(tn)
                    got = song_step(dev, s["bar"], t, (int(st) - 1) & 15) if t >= 1 else None
                    if got != int(want):
                        fail(ln, f"{tn} step {st} is {got}, expected {want}")
                elif arg in ("phrases", "phrase_len"):
                    tn, *nums = more.split()
                    ph = song_phrases(dev, TRACKS.index(tn))
                    got = len(ph) if arg == "phrases" else (ph[int(nums[0])] if int(nums[0]) < len(ph) else -1)
                    if got != int(nums[-1]):
                        fail(ln, f"{tn} {arg} is {got}, expected {nums[-1]}")
                elif arg == "nudge" and not more.startswith("Voice"):
                    tn, st, want = more.split()
                    got = song_step(dev, s["bar"], TRACKS.index(tn), (int(st) - 1) & 15, nudge=True)
                    if got != int(want):
                        fail(ln, f"{tn} step {st} nudge {got}, expected {want}")
                elif arg == "sung":
                    if bool(level_seen) != bool(int(more)):
                        fail(ln, f"sung level {level_seen}, expected {'some' if int(more) else 'none'}")
                    level_seen = 0
                elif arg in ("saved", "empty"):
                    want = 1 if arg == "saved" else 2
                    for _ in range(8):                  # a flash write takes a moment
                        if dev.slots()[int(more)][0] == want:
                            break
                        time.sleep(0.5)
                    else:
                        fail(ln, f"slot S{int(more) + 1} {'not saved' if want == 1 else 'holds something'}")
                else:
                    skipped.add(arg)
            else:
                skipped.add(cmd)
    finally:
        dev.buttons = dev.keys = 0
        dev.hold()
        try:                                              # a solo is a change to the song: put every track back
            for t in range(4):
                if (dev.screen()["mutes"] >> t) & 1:
                    dev.buttons |= 1 << BTN.index("HOME")
                    run(150)
                    dev.keys |= 1 << (1, 3, 5, 8)[t]
                    run(120)
                    dev.keys &= ~(1 << (1, 3, 5, 8)[t])
                    run(150)
                    dev.buttons &= ~(1 << BTN.index("HOME"))
                    run(250)
        except Exception:
            pass
        dev.buttons = dev.keys = 0
        dev.hold()
        stop_recording(rec)
    print(f"{a.script}: {fails} failures; skipped (the simulator only): {', '.join(sorted(skipped)) or 'none'}")
    sim_wav = Path("build/sim") / a.script.stem / "audio.wav"
    if a.wav and a.wav.exists() and sim_wav.exists():
        print(f"audio, device against the simulator ({sim_wav}):")
        sys.path.insert(0, str(Path(__file__).parent))
        import audio_compare
        if audio_compare.compare(a.wav, sim_wav):
            fails += 1
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
