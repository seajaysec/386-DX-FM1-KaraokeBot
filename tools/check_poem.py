#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Every word and template SLAMJAM can say (firmware/src/app/poem.txt), singular and plural, said by the firmware's
own speech engine in the simulator (build/host/sim, built with the address and undefined-behaviour sanitizers): a
word that reaches a phoneme the US-English voice left out (tools/espeak_prune.txt) stops it there.
    tools/check_poem.py"""
import re, subprocess, sys, tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
words, core = [], False
for raw in (root / "firmware/src/app/poem.txt").read_text().splitlines():
    line = raw.strip()
    if line.startswith("@"):
        core = line.startswith("@core ")
    if not line or line.startswith("#") or line.startswith("@"):
        continue
    m = re.match(r"^([A-Za-z]):(.*)$", "f:" + line if core else line)
    if not m:
        words.append(re.sub(r"\{[A-Za-z]+\}", "", line).strip())
        continue
    for w in (x.strip() for x in m.group(2).split(",") if x.strip()):
        one, many = w.split("/") if "/" in w else (w, w + "s" if m.group(1) in "Hnx" else None)
        words += [one] + ([many] if many else [])
words = list(dict.fromkeys(w for w in words if any(c.isalpha() for c in w)))
with tempfile.TemporaryDirectory() as d:
    sim = Path(d) / "say.sim"
    sim.write_text("foreign\nwait 2000\n" + "".join(f"say {w}\n" for w in words))
    r = subprocess.run([str(root / "build/host/sim"), str(sim), d], capture_output=True, text=True)
    out = r.stdout + r.stderr
    if r.returncode or "FAIL" in out:
        said = [l[4:] for l in r.stderr.splitlines() if l.startswith("say ")]
        print("\n".join(l for l in out.splitlines() if "FAIL" in l or "ERROR" in l))
        if said:
            print("stopped at:", said[-1])
        sys.exit(1)
print(f"check_poem: {len(words)} words and templates said")
