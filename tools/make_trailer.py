#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the trailer: a scene script played on the simulator (the firmware's own code, its screen 30 frames
a second and its audio sample for sample) at 175 ms an action, then edited into a square video with titles.

    tools/make_trailer.py [out.mp4]

Needs the simulator (tools/build_host.py), ffmpeg and Pillow."""
import os, re, subprocess, sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build/trailer.mp4"
work = root / "build/trailer"
work.mkdir(parents=True, exist_ok=True)

BPM = 116
S = []                                   # the script's lines
entry_len = 4                            # the length of the next note written (sixteenths)


def say(*lines):
    S.extend(lines)


def mark(text):
    S.append(f"mark {text}")


def tap(b, n=1):
    S.extend([f"tap {b}"] * n)


def turn(enc, n):
    if n:
        S.append(f"turn {enc} {n}")


def length(n):
    global entry_len
    turn("K4", n - entry_len)
    entry_len = n


def line_notes(notes):
    """notes: (key, length in sixteenths, how long the key is held in ms)"""
    tap("REC")
    for key, ln, hold in notes:
        length(ln)
        S.append(f"key {key} {hold}")
    tap("REC")


# ---- the sounds: PRESETS steps through a track's own list ----
BASS_SOUNDS = [40, 41, 42, 43, 44, 45, 46, 47, 4, 8, 13, 18, 27, 36]
KEYS_SOUNDS = [p for p in range(48) if p not in BASS_SOUNDS and p != 7]
GLASS, WOBBLE, CRUNCH = 12, 45, 5        # glass keys, wobble (the filter in eighth notes), the Crunch kit
KEYS_DEFAULT, BASS_DEFAULT = 24, 8

# ================================================================ the script
say("foreign", "# the 386DX trailer: new song to a full band, at 175 ms an action")
mark("386DX")
say("wait 2900")                          # the title is over the screen until 2.6 s: nothing happens under it

# --- a new song, in the first empty slot
mark("a new song")
tap("GLO"), tap("SEL"), turn("SELECT", 2), tap("SEL", 2)

# --- line 1: the words, the pitches, the lengths. The robot's first words.
mark("type the words")
tap("SEL", 2)
say("type whatup fam", "tap SEL")
mark("pitch and length, a key a syllable")
line_notes([(9, 4, 300), (11, 4, 300), (14, 8, 900)])
entry_len_after = 8
entry_len = 8
mark("it sings")
tap("HOME")                               # Sing: the robot
say("tap PLAY", "wait 2600", "tap PLAY")
tap("HOME")                               # Song again

# --- line 2: nice to meet you fam
turn("SELECT", 1)
tap("SEL", 2)
say("type nice to meet you all", "tap SEL")
line_notes([(14, 4, 220), (11, 4, 220), (14, 4, 220), (16, 4, 220), (14, 8, 800)])

# --- line 3, written after it: I am three eight six dee ex
turn("SELECT", 1)
tap("SEL", 2)
say("type i am three eight six dee ex", "tap SEL")
line_notes([(7, 4, 200), (9, 4, 200), (11, 4, 200), (14, 4, 200), (16, 4, 200), (14, 4, 200), (19, 12, 1000)])

# --- it belongs before the other: move it up
mark("move a line")
tap("SEL"), turn("SELECT", 3), tap("SEL"), turn("SELECT", -1), tap("SEL")

# --- a second row sings the same tune with other words: a row, its line, a copy of it, new words
mark("copy a line")
turn("SELECT", 1)                         # the "nice to meet you fam" row
turn("SELECT", 1)                         # the end
tap("SEL", 2), tap("HOME")                # a new row, no words yet
turn("PRESET", -2)                        # it sings line 2, shared with the row above
tap("SEL"), turn("SELECT", 2), tap("SEL")  # COPY AS NEW LINE: its own, to change
tap("SEL", 2)                             # EDIT WORDS
tap("OCT-", 12)                           # "meet you all"
say("type sing with you", "tap SEL")

# --- the first line again, by reference
mark("a row can sing a line again")
turn("SELECT", 1)
tap("SEL", 2), tap("HOME")
turn("PRESET", -4)

# --- the tempo, and the instruments
mark("tempo")
say("down HOME", f"turn SELECT {BPM - 100}", "up HOME")
mark("pick the sounds")
tap("EDIT")
turn("ALGO", 1)
turn("PRESET", KEYS_SOUNDS.index(GLASS) - KEYS_SOUNDS.index(KEYS_DEFAULT))
say("wait 700")
turn("ALGO", 1)
turn("PRESET", BASS_SOUNDS.index(WOBBLE) - BASS_SOUNDS.index(BASS_DEFAULT))
say("wait 700")
turn("ALGO", 1)
turn("PRESET", CRUNCH)
say("wait 700")

# --- play it, and build the band live, while the voice sings
mark("play, and record the band live")
tap("HOME")                               # Sing
turn("ALGO", -2)                          # Keys
say("tap PLAY", "waitstep 8")
tap("REC")
KEYS = [(0, 7), (2, 11), (4, 14), (6, 19), (8, 14), (10, 11), (12, 9), (14, 11)]
for step, key in KEYS:
    say(f"waitstep {step}", f"key {key} 50")
tap("REC")
mark("keys")
tap("SEQ")                                # its piano roll, looping
say("wait 2500")
tap("HOME")
turn("ALGO", 1)                           # Bass
tap("REC")
BASS = [(0, 7), (3, 7), (6, 7), (8, 14), (11, 14), (14, 12)]
for step, key in BASS:
    say(f"waitstep {step}", f"key {key} 50")
tap("REC")
mark("bass")
tap("SEQ")
say("wait 2500")
tap("HOME")
turn("ALGO", 1)                           # Drums
tap("REC")
for step in (0, 4, 8, 12):
    say(f"waitstep {step}", "key 1 40")   # kick
for step in (4, 12):
    say(f"waitstep {step}", "key 3 40")   # snare
tap("REC")
mark("step entry for the drums")
tap("SEQ")
say("key 25 60")                          # the closed hat is the lane
for key in (0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26):
    say(f"key {key} 50")                  # a hat on every step
say("key 15 60")                          # the clap
for key in (4, 21):
    say(f"key {key} 50")

# --- punch-in effects
mark("punch-in effects")
tap("FX")
# two very different ones: the brrr (key 7, 1/32 slices: white key 11) and the phone (key 3: white key 4)
say("wait 700", "keydown 11", "wait 1100", "keyup 11", "wait 500", "keydown 4", "wait 1500", "keyup 4", "wait 400")

# --- change the voice while it sings
mark("shape the voice, live")
tap("HOME")
for enc, n in (("K1", 25), ("K2", 30), ("K3", -20), ("K4", 30)):
    turn(enc, n)
    say("wait 250")
tap("EDIT"), turn("ALGO", -3)
for enc, n in (("K2", 30), ("K3", 30), ("K4", 1)):
    turn(enc, n)
    say("wait 300")
tap("ENV")
for enc, n in (("K1", 40), ("K2", 25)):
    turn(enc, n)
    say("wait 300")
tap("HOME")
say("wait 1500")

# --- the finish: stop, and play "nice to meet you all" once more from its row; on "all" (tick 64) a tape-off
mark("tape off")
say("tap PLAY")
tap("HOME")                               # Song: its rows
turn("SELECT", -2)                        # the row, "nice to meet you all"
say("tap PLAY")
tap("HOME")                               # the robot
say("waittick 63", "down HOME", "keydown 14", "wait 2100", "keyup 14", "up HOME", "wait 500")
mark("end")
say("tap PLAY", "wait 600")

script = work / "trailer.sim"
script.write_text("\n".join(S) + "\n")

# ================================================================ run it
env = dict(os.environ, SIM_PACE="175", SIM_NOSTALL="1", SIM_VIDEO=str(work / "frames.rgb"))
r = subprocess.run([str(root / "build/host/sim"), str(script), str(work)], env=env, capture_output=True, text=True,
                   cwd=root)
print(r.stdout.strip().splitlines()[-1])
if r.returncode:
    print(r.stdout[-2000:], r.stderr[-2000:])
    raise SystemExit("the script did not run cleanly")
marks = [(int(m.group(1)), m.group(2)) for m in re.finditer(r"^MARK (\d+) (.*)$", r.stdout, re.M)]
print(len(marks), "captions")


# ================================================================ edit it: a square frame, titles, the sound
from PIL import Image, ImageDraw, ImageFont

FONT = str(root / "assets/fonts/BarlowSemiCondensed-Bold.ttf")
INK, PAPER, CORAL, DARK = (38, 36, 34), (244, 241, 232), (233, 77, 53), (29, 26, 23)
SIZE, SCREEN, MARGIN = 1080, 960, 60
frames = (work / "frames.rgb").stat().st_size // (240 * 240 * 3)
seconds = frames / 30.0
cards = work / "cards"
cards.mkdir(exist_ok=True)


def font(px):
    return ImageFont.truetype(FONT, px)


def centred(draw, y, text, px, fill):
    f = font(px)
    w = draw.textlength(text, font=f)
    draw.text(((SIZE - w) / 2, y), text, font=f, fill=fill)


# the caption each moment gets, on the band under the screen (what the viewer is watching)
CAPTION = {"a new song": "start a new song", "type the words": "type the words", "pitch and length, a key a syllable":
           "a key for each syllable: its pitch, its length", "it sings": "and it sings", "move a line": "move a line",
           "copy a line": "copy a line, change its words", "a row can sing a line again": "or sing a line again, by reference",
           "tempo": "set the tempo", "pick the sounds": "pick the sounds", "play, and record the band live":
           "play it, and record the band live", "keys": "keys", "bass": "bass", "step entry for the drums":
           "drums: step entry", "punch-in effects": "punch-in effects", "shape the voice, live": "shape the voice, live"}
# the quiet stretch of editing (move, copy, tempo, the sounds) plays 1.5x: the sound there is only previews
mt = {text: ms / 1000.0 for ms, text in marks}
FAST_FROM, FAST_TO, FAST = mt["move a line"], mt["play, and record the band live"], 1.5
segments = [(0.0, FAST_FROM, 1.0), (FAST_FROM, FAST_TO, FAST), (FAST_TO, seconds, 1.0)]


def retime(t):
    """a time in the recording as it falls in the edit"""
    out = 0.0
    for a, b, k in segments:
        if t <= a:
            break
        out += (min(t, b) - a) / k
    return out


total = retime(seconds)
caps = []
for i, (ms, text) in enumerate(marks):
    if text in CAPTION:
        end = marks[i + 1][0] if i + 1 < len(marks) else int(seconds * 1000)
        caps.append((retime(ms / 1000.0), retime(end / 1000.0), CAPTION[text]))
inputs, chain, last = [], [], "[bg]"
for k, (t0, t1, text) in enumerate(caps):
    im = Image.new("RGBA", (SIZE, MARGIN), DARK + (255,))
    centred(ImageDraw.Draw(im), 4, text, 44, PAPER)
    im.save(cards / f"cap{k}.png")
# the top band: the name, always
top = Image.new("RGBA", (SIZE, MARGIN), DARK + (255,))
d = ImageDraw.Draw(top)
d.text((MARGIN, 2), "386DX", font=font(54), fill=CORAL)
f = font(26)
d.text((SIZE - MARGIN - d.textlength("the FM-1's singing robot", font=f), 16), "the FM-1's singing robot", font=f, fill=PAPER)
top.save(cards / "top.png")
# the title over the first moments, and the end card
title = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
d = ImageDraw.Draw(title)
d.rectangle((0, 640, SIZE, 900), fill=DARK + (255,))
centred(d, 650, "386DX", 200, CORAL)
centred(d, 850, "a singing robot with a band", 40, PAPER)
title.save(cards / "title.png")
end = Image.new("RGB", (SIZE, SIZE), DARK)
d = ImageDraw.Draw(end)
centred(d, 300, "386DX", 330, CORAL)
centred(d, 650, "a singing robot with a band", 64, PAPER)
centred(d, 740, "for the M-VAVE FM-1", 52, (190, 184, 170))
end.save(cards / "end.png")

END_S = 3.5
args = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "240x240", "-r", "30",
        "-i", str(work / "frames.rgb"), "-i", str(work / "audio.wav"), "-i", str(cards / "top.png"), "-i", str(cards / "title.png"),
        "-loop", "1", "-framerate", "30", "-t", str(END_S), "-i", str(cards / "end.png")]
for k in range(len(caps)):
    args += ["-i", str(cards / f"cap{k}.png")]
f = []
for i, (a, b, k) in enumerate(segments):
    f.append(f"[0:v]trim=start={a:.3f}:end={b:.3f},setpts=(PTS-STARTPTS)/{k}[sv{i}]")
    f.append(f"[1:a]atrim=start={a:.3f}:end={b:.3f},asetpts=PTS-STARTPTS" + (f",atempo={k}" if k != 1.0 else "") + f"[sa{i}]")
f.append("".join(f"[sv{i}][sa{i}]" for i in range(len(segments))) + f"concat=n={len(segments)}:v=1:a=1[cv][ca]")
f += [f"[cv]scale={SCREEN}:{SCREEN}:flags=neighbor,pad={SIZE}:{SIZE}:{MARGIN}:{MARGIN}:color=0x1d1a17[bg]",
     "[bg][2:v]overlay=0:0[v1]", f"[v1][3:v]overlay=0:0:enable='between(t,0.1,2.6)'[v2]"]
prev = "v2"
for k, (t0, t1, text) in enumerate(caps):
    f.append(f"[{prev}][{5 + k}:v]overlay=0:{SIZE - MARGIN}:enable='between(t,{t0:.2f},{t1:.2f})'[c{k}]")
    prev = f"c{k}"
f.append(f"[{prev}]fade=t=in:st=0:d=0.3[main]")
f.append(f"[4:v]fade=t=in:st=0:d=0.6,setsar=1[endcard]")
f.append(f"[main]fps=30,settb=1/30,format=yuv420p,setsar=1[m];[endcard]fps=30,settb=1/30,format=yuv420p[e];[m][e]xfade=transition=fade:duration=0.8:offset={total - 0.8:.2f}[vout]")
#[endcard]format=yuv420p[e];[m][e]xfade=transition=fade:duration=0.8:offset={total - 0.8:.2f}[vout]")
f.append(f"[ca]afade=t=in:st=0:d=0.2,afade=t=out:st={total + END_S - 3.2:.2f}:d=3.0,apad=whole_dur={total + END_S - 0.8:.2f}[aout]")
args += ["-filter_complex", ";".join(f), "-map", "[vout]", "-map", "[aout]", "-c:v", "libx264", "-preset", "slow", "-crf", "16",
         "-pix_fmt", "yuv420p", "-r", "30", "-c:a", "aac", "-b:a", "256k", "-shortest", "-movflags", "+faststart", str(out)]
r = subprocess.run(args, capture_output=True, text=True)
if r.returncode:
    print(r.stderr[-3000:])
    raise SystemExit("ffmpeg failed")
print(f"{out}  {total + END_S - 0.8:.1f} s, {SIZE}x{SIZE}")
