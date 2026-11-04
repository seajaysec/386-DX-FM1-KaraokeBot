#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Packs firmware/src/app/poem.txt (SLAMJAM's words) into a C header the poet reads (poem.h): every template and
word as one string, its characters merged in pairs and Huffman coded, with a table
saying where each group starts: each frame's kinds of template (a kind the frame leaves out is the first frame's),
each topic's classes of word, then the wildcards. Checks that no template, filled with the longest words it can
is likely to take (each slot's three-quarter length) passes 63 characters (a title 23); a poem tries again when one
does run over.
    tools/gen_poem.py build/gen/poem_data.h"""
import collections, heapq, re, sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
KINDS = ["intro", "title", "open", "image", "ask", "rise", "vow", "refrain", "return", "close"]
CLASSES = ["H", "n", "a", "p", "v"]
WILD = ["x", "X", "l", "m", "k", "e", "s", "N", "t", "d", "g", "G"]
frames, topics, wild, cores = [], [], {w: [] for w in WILD}, []
cur = None
for raw in (root / "firmware/src/app/poem.txt").read_text().splitlines():
    line = raw.strip()
    if not line or line.startswith("#"):
        continue
    if line.startswith("@frame "):
        frames.append({"name": line[7:].strip(), **{k: [] for k in KINDS}})
        cur = None
        continue
    if line.startswith("@topic "):
        topics.append({"name": line[7:].strip(), **{c: [] for c in CLASSES}})
        cur = ("words", topics[-1])
        continue
    if line == "@wild":
        cur = ("words", wild)
        continue
    if line.startswith("@core "):
        cores.append({"name": line[6:].strip(), "f": []})
        cur = ("core", cores[-1])
        continue
    if line.startswith("@"):
        cur = ("kind", frames[-1][line[1:]])
        continue
    if cur[0] == "kind":
        cur[1].append(line)
    elif cur[0] == "core":
        cur[1]["f"].extend(w.strip() for w in line.split(",") if w.strip())
    else:
        cls, words = line.split(":", 1)
        cur[1][cls.strip()].extend(w.strip() for w in words.split(",") if w.strip())

def forms(w):                                   # singular, plural
    return (w.split("/")[0], w.split("/")[1]) if "/" in w else (w, w + "s")

# each slot's fills' lengths, over every topic (a template may meet any); a topic's noun, adjective, place or verb
# may be taken by a wildcard's (poem.h), so those count too
fill = {"{T}": [23]}
for c in "Hn":
    fill["{%s}" % c] = [len(forms(w)[0]) for t in topics for w in t[c]]
    fill["{%ss}" % ("H" if c == "H" else "n")] = [len(forms(w)[1]) for t in topics for w in t[c]]
fill["{HS}"] = fill.pop("{Hs}")
for c in "apv":
    fill["{%s}" % c] = [len(w) for t in topics for w in t[c]]
for c in WILD:
    fill["{%s}" % c] = [len(forms(w)[0]) for w in wild[c]]
fill["{xs}"] = [len(forms(w)[1]) for w in wild["x"]]
fill["{f}"] = [len(w) for c in cores for w in c["f"]]
for a, b in (("n", "x"), ("ns", "xs"), ("a", "X"), ("p", "l"), ("v", "m")):
    fill["{%s}" % a] += fill["{%s}" % b]
# a template must fit when filled with each slot's three-quarter length (poem.h tries again when a line runs over)
typical = {k: sorted(v)[len(v) * 3 // 4] for k, v in fill.items()}
bad = []
for f in frames:
    for k in KINDS:
        for t in f[k]:
            for slot in re.findall(r"\{[A-Za-z]+\}", t):
                if slot not in typical or (slot == "{T}") != (k == "intro"):
                    raise SystemExit(f"gen_poem: '{t}': no slot {slot} in {k}")
            n = len(re.sub(r"\{[A-Za-z]+\}", lambda m: "x" * typical[m.group(0)], t)) + t.count("{")  # (an "an")
            cap = 23 if k == "title" else 63
            if n > cap:
                bad.append(f"gen_poem: '{t}' can reach {n} characters (at most {cap})")
if bad:
    raise SystemExit("\n".join(bad))
for k in KINDS:
    if not frames[0][k]:
        raise SystemExit(f"gen_poem: the first frame has no {k}")
    for f in frames:
        if len(f[k]) > 32:
            raise SystemExit(f"gen_poem: {f['name']} has more than 32 of {k}")

groups = []                                     # each group's strings, in the table's order
for f in frames:
    for k in KINDS:
        groups.append(f[k] if f[k] else None)   # (None: the first frame's)
for t in topics:
    for c in CLASSES:
        groups.append(t[c])
for c in WILD:
    groups.append(wild[c])
for c in cores:
    groups.append(c["f"])
groups.append([t["name"] for t in topics])
groups.append([f["name"] for f in frames])
groups.append([c["name"] for c in cores])
for g in groups:
    for s in g or []:
        assert all(32 <= ord(c) < 127 for c in s), s

# the strings as symbols: each character used (0 ends a string), then pairs of symbols merged while the merge pays,
# up to 256 symbols in all; then every symbol given a canonical Huffman code, its bits read high first
seqs = [[list(s.encode()) + [0] for s in g] if g else None for g in groups]
chars = sorted({c for g in seqs if g for q in g for c in q})
sym = {c: i for i, c in enumerate(chars)}
seqs = [[[sym[c] for c in q] for q in g] if g else None for g in seqs]
end = sym[0]
pairs = []
while len(chars) + len(pairs) < 256:
    count = collections.Counter(p for g in seqs if g for q in g for p in zip(q, q[1:]) if end not in p)
    if not count:
        break
    pair, n = max(count.items(), key=lambda kv: (kv[1], -kv[0][0], -kv[0][1]))
    if n < 4:
        break
    tok = len(chars) + len(pairs)
    pairs.append(pair)
    for g in seqs:
        for k, q in enumerate(g or []):
            r, i = [], 0
            while i < len(q):
                if i + 1 < len(q) and (q[i], q[i + 1]) == pair:
                    r.append(tok); i += 2
                else:
                    r.append(q[i]); i += 1
            g[k] = r
nsym = len(chars) + len(pairs)
freq = collections.Counter(t for g in seqs if g for q in g for t in q)
heap = [(n, i, (t,)) for i, (t, n) in enumerate(sorted(freq.items()))]
heapq.heapify(heap)
length = {t: 0 for t in freq}
uid = len(heap)
while len(heap) > 1:
    a, b = heapq.heappop(heap), heapq.heappop(heap)
    for t in a[2] + b[2]:
        length[t] += 1
    heapq.heappush(heap, (a[0] + b[0], uid, a[2] + b[2]))
    uid += 1
assert max(length.values()) <= 16
order = sorted(freq, key=lambda t: (length[t], t))  # canonical: by length, then symbol
counts = [sum(1 for t in order if length[t] == L) for L in range(1, 17)]
code, c, prev = {}, 0, 0
for t in order:
    c <<= length[t] - prev
    prev = length[t]
    code[t] = (c, length[t])
    c += 1
data, table = [], []
for gi, g in enumerate(seqs):
    if g is None:
        table.append(table[gi % len(KINDS)])
        continue
    table.append((len(data), len(g)))
    bits = "".join(format(code[t][0], "0%db" % code[t][1]) for q in g for t in q)
    bits += "0" * (-len(bits) % 8)                         # (each group starts on a byte)
    data += [int(bits[i:i + 8], 2) for i in range(0, len(bits), 8)]
assert len(data) < 65536
out = ["/* generated by tools/gen_poem.py from firmware/src/app/poem.txt: do not edit */",
       "enum { " + ", ".join(f"PK_{k.upper()}" for k in KINDS) + ", NPK };",
       "enum { PC_HERO, PC_NOUN, PC_ADJ, PC_PLACE, PC_VERB, NPC };",
       f'#define POEM_WILD "{"".join(WILD)}"      /* the wildcards\' slot letters, in their groups\' order */',
       f"#define POEM_FRAMES {len(frames)}",
       f"#define POEM_TOPICS {len(topics)}",
       f"#define POEM_CORES {len(cores)}",
       f"#define POEM_CHARS {len(chars)}",
       "/* the symbols: a character each, then pairs of symbols; the codes: how many of each length (1..16), and the",
       " * symbols in code order */",
       f"static const uint8_t POEM_CHAR[{len(chars)}] = {{" + ", ".join(map(str, chars)) + "};",
       f"static const uint8_t POEM_PAIR[{len(pairs)}][2] = {{" + ", ".join(f"{{{a}, {b}}}" for a, b in pairs) + "};",
       "static const uint8_t POEM_COUNT[16] = {" + ", ".join(map(str, counts)) + "};",
       f"static const uint8_t POEM_ORDER[{len(order)}] = {{" + ", ".join(map(str, order)) + "};",
       "/* each group: the byte its strings start at in POEM_DATA, how many (each ends in the character 0) */",
       f"static const uint16_t POEM_GROUP[{len(table)}][2] = {{" + ", ".join(f"{{{a}, {b}}}" for a, b in table) + "};",
       f"static const uint8_t POEM_DATA[{len(data)}] = {{" + ", ".join(map(str, data)) + "};", ""]
Path(sys.argv[1]).write_text("\n".join(out))
raw = sum(len(s) + 1 for g in groups if g for s in g)
print(f"gen_poem: {len(frames)} frames, {len(topics)} topics, {len(cores)} cores, {raw} bytes as {len(data)} + "
      f"{len(chars) + 2 * len(pairs) + 16 + len(order)} (symbols, codes) + {4 * len(table)} (groups)")
