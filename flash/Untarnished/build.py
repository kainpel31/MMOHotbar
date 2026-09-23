# Build Interface/STB_Keycaps.swf (Untarnished UI set) from Untarnished's
# favoritesmenu.swf. See README.md next to this file for what each step does.
#
#   python build.py            # expects favoritesmenu.swf in the working directory
#
# Nothing here is redistributable: run it against the copy of Untarnished you already
# have installed. JPEXS FFDec is required and trips over paths with spaces or brackets,
# so work in a plain folder such as C:\temp\keycaps.
import re, subprocess, struct, zlib, os, sys

FFDEC = r"C:\Program Files (x86)\FFDec\ffdec-cli.exe"
SRC   = "favoritesmenu.swf"
WORK  = "work"
ROOT  = 157            # DefineSprite holding the Keyboard/Mouse/Gamepad frames
NAME  = b"STBKeycap"

def dump(swf, out):
    with open(out, "w", encoding="utf-8") as f:
        subprocess.run([FFDEC, "-dumpSWF", swf], stdout=f, stderr=subprocess.DEVNULL, check=True)
    return open(out, encoding="utf-8", errors="ignore").read().split("\n")

line_re = re.compile(r'^([0-9a-fA-F]+):(\s+)(\d+)\.\s+(\S+)(.*)$')
chid_re = re.compile(r'\(chid:\s*(\d+)')

def parse(lines):
    rows = []
    for ln in lines:
        m = line_re.match(ln.rstrip())
        if not m:
            continue
        cm = chid_re.search(m.group(5))
        rows.append((len(m.group(2)), int(m.group(3)), m.group(4), m.group(5),
                     int(cm.group(1)) if cm else None))
    return rows

# ---- 1. characters reachable from the keycap clip -------------------------
rows = parse(dump(SRC, os.path.join(WORK, "tags1.txt")))
defs, edges, stack = {}, {}, []
for indent, idx, tag, rest, chid in rows:
    while stack and indent <= stack[-1][0]:
        stack.pop()
    if (tag.startswith("Define") or tag.startswith("ImportAssets")) and chid is not None:
        defs[chid] = tag
        if tag.startswith("DefineSprite"):
            edges.setdefault(chid, set())
            stack.append((indent, chid))
    elif tag.startswith("PlaceObject") and chid is not None and stack:
        edges[stack[-1][1]].add(chid)

keep, todo = set(), [ROOT]
while todo:
    c = todo.pop()
    if c in keep:
        continue
    keep.add(c)
    todo.extend(n for n in edges.get(c, ()) if n not in keep)

# ImportAssets stubs are not removable this way; they are tiny and unreferenced.
drop = sorted(c for c in defs if c not in keep and defs[c].startswith("Define"))
print("defined %d, closure(%d) %d, removing %d" % (len(defs), ROOT, len(keep), len(drop)))

stripped = os.path.join(WORK, "stripped.swf")
subprocess.run([FFDEC, "-removeCharacter", SRC, stripped] + [str(c) for c in drop],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

# ---- 2. strip scaffolding and export the clip as STBKeycap ----------------
# Done on the raw tag stream: only the main timeline is walked, DefineSprite bodies
# stay opaque, so "top level" needs no guessing.
raw = open(stripped, "rb").read()
ver = raw[3]
body = zlib.decompress(raw[8:]) if raw[:3] == b"CWS" else raw[8:]

pos = ((5 + 4 * (body[0] >> 3) + 7) // 8) + 4      # FrameSize RECT + frame rate + frame count
DROP = {12, 56, 57, 71}                            # DoAction, ExportAssets, ImportAssets(2)
out_tags, p = [], pos
while p < len(body):
    tl, = struct.unpack_from("<H", body, p)
    code, ln = tl >> 6, tl & 0x3F
    hdr = 2
    if ln == 0x3F:
        ln, = struct.unpack_from("<I", body, p + 2); hdr = 6
    if code == 0:
        break
    if code not in DROP:
        out_tags.append(body[p:p + hdr + ln])
    p += hdr + ln

payload = struct.pack("<HH", 1, ROOT) + NAME + bytes([0])
out_tags.append(struct.pack("<H", (56 << 6) | len(payload)) + payload)
new = body[:pos] + b"".join(out_tags) + bytes([0, 0])

out = "STB_Keycaps.swf"
open(out, "wb").write(b"CWS" + bytes([ver]) + struct.pack("<I", 8 + len(new)) + zlib.compress(new, 9))
print("wrote", out, os.path.getsize(out), "bytes")
