#!/usr/bin/env python3
"""The companion page as a folder to publish: the page, the browser engine, the emulator and the
firmware package it installs (with the licence texts the package needs).
Build the firmware first (tools/build.py) and the browser parts (tools/build_web.py)."""
import argparse, shutil
from pathlib import Path

root = Path(__file__).resolve().parents[1]
web = root / "web"
a = argparse.ArgumentParser()
a.add_argument("--out", type=Path, default=root / "build/companion")
args = a.parse_args()

package = web / "firmware/386dx.fwsc"
if not package.exists():
    raise SystemExit(f"{package} is missing: run tools/build.py first")
if args.out.exists():
    shutil.rmtree(args.out)
files = [p for p in web.glob("*") if p.suffix in {".html", ".css", ".mjs"}]
for d in ("dsp", "emu", "fonts"):
    files += [p for p in (web / d).rglob("*") if p.is_file() and p.suffix != ".c"]
files.append(package)
files.append(web / "firmware/386dx.json")     # its build and checksum: what the page compares the FM-1 with
files.append(web / "changelog.json")      # what changed in each version, for the page
# the song library the page offers (public-domain standards)
files.append(web / "songs/karaoke-library.json")
files += sorted((web / "songs/library").glob("*"))
for src in files:
    dst = args.out / src.relative_to(web)
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)
# the package contains Apache-2.0 SDK files and links GPL and permissive libraries
lic = args.out / "firmware/LICENSES"
lic.mkdir(parents=True)
for f in sorted((root / "LICENSES").glob("*.txt")):
    shutil.copy(f, lic / f.name)
for doc in ("LICENSE", "LICENSING.md"):
    shutil.copy(root / doc, args.out / "firmware" / doc)
print("Companion:", args.out, f"({len(files)} files)")
