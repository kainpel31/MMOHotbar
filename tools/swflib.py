#!/usr/bin/env python3
# swflib.py - minimal SWF v8 binary writer (subset for Skyrim/GFx assets).
# Spec-critical points: tag header is u16 (code<<6 | len or 0x3F + u32),
# shape records live in ONE continuous bitstream, StyleChangeRecord flag
# order is NewStyles,LineStyle,FillStyle1,FillStyle0,MoveTo with MoveTo data
# FIRST, then FillStyle0/FillStyle1 values; edge NumBits field = bits-2.
import struct, zlib


class BitW:
    __slots__ = ("buf", "cur", "n")

    def __init__(self):
        self.buf = bytearray()
        self.cur = 0
        self.n = 0

    def bit(self, v):
        self.cur = (self.cur << 1) | (1 if v else 0)
        self.n += 1
        if self.n == 8:
            self.buf.append(self.cur)
            self.cur = 0
            self.n = 0

    def bits(self, v, n):
        for i in range(n - 1, -1, -1):
            self.bit((v >> i) & 1)

    def sbits(self, v, n):
        # Coerce to int: callers naturally pass float scales (twips * 65536).
        self.bits(int(v) & ((1 << n) - 1), n)

    def align(self):
        if self.n:
            self.buf.append((self.cur << (8 - self.n)) & 0xFF)
            self.cur = 0
            self.n = 0

    def data(self):
        self.align()
        return bytes(self.buf)


def sb_bits(v):
    n = 1
    while not (-(1 << (n - 1)) <= v <= (1 << (n - 1)) - 1):
        n += 1
    return n


def tag_bytes(code, body=b""):
    c = code << 6
    if len(body) < 0x3F:
        return struct.pack("<H", c | len(body)) + body
    return struct.pack("<HI", c | 0x3F, len(body)) + body


def rect(vals):
    nb = max(sb_bits(v) for v in vals)
    b = BitW()
    b.bits(nb, 5)
    for v in vals:
        b.sbits(v, nb)
    return b.data()


def matrix(scale=None, translate=(0, 0)):
    b = BitW()
    if scale:
        raw = int(round(scale * 65536))
        nb = sb_bits(raw)
        b.bit(1)
        b.bits(nb, 5)
        b.sbits(raw, nb)
        b.sbits(raw, nb)
    else:
        b.bit(0)
    b.bit(0)  # no rotate
    tx, ty = translate
    tb = max(sb_bits(tx), sb_bits(ty))
    b.bits(tb, 5)
    b.sbits(tx, tb)
    b.sbits(ty, tb)
    return b.data()


def header(w, h, fps=30, frames=1):
    b = bytearray(b"FWS")
    b.append(8)  # version
    b += struct.pack("<I", 0)  # length placeholder
    b += rect([0, w, 0, h])
    b += struct.pack("<H", int(fps * 256))
    b += struct.pack("<H", frames)
    return b


def file_attributes():
    return tag_bytes(69, b"\x00\x00\x00\x00")


def set_background(r, g, b):
    return tag_bytes(9, bytes((r, g, b)))


def define_bits_lossless2(char_id, argb_bytes, w, h):
    body = struct.pack("<HBHH", char_id, 5, w, h) + zlib.compress(argb_bytes, 9)
    return tag_bytes(36, body)


def define_shape2(shape_id, bmp_id, bounds=(0, 1280, -1280, 0), scale=20.0):
    """A shape that paints bmp_id once, unscaled inside `bounds` (twips).

    `scale` is the twips-per-pixel of the bitmap's own coordinate space; the
    generator keeps it at 20.0 so the art lands 1:1 at the usual Flash layout of
    20 twips per pixel.

    Field widths follow the DefineShape2 spec exactly, because every width is
    a bit field and a single wrong width desynchronises the rest of the record:
    NumFillStyles and NumLineStyles are UB[4] (not UB[8]), and a fill style
    type is a UB[4] field whose low 2 bits select the kind.
    """
    xmin, xmax, ymin, ymax = bounds
    out = bytearray()
    out += struct.pack("<H", shape_id)
    out += rect([xmin, xmax, ymin, ymax])

    # SHAPEWITHSTYLE stores the two style ARRAYS first and their counts after
    # them, not the other way round. Writing the counts first desynchronises
    # every following bit field, which is what made the old generator emit a
    # shape FFDec could not read.
    b = BitW()

    # --- FillStyleArray (1 entry) ---
    # FillStyleType UB[4]: values 4-7 are the bitmap kinds, with bit 2 selecting
    # the wrap mode. 7 = non-smoothed bitmap, repeated.
    b.bits(7, 4)
    b.bits(bmp_id, 16)
    # Bitmap matrix: draw the bitmap unscaled with its top-left pixel on the
    # shape's top-left corner (xmin, ymax).
    b.bit(1)  # HasScale
    b.bits(22, 5)
    b.sbits(scale * 65536, 22)
    b.sbits(scale * 65536, 22)
    b.bit(0)  # HasRotate = false
    b.bits(12, 5)
    b.sbits(xmin, 12)
    b.sbits(ymax, 12)

    # --- LineStyleArray: empty, so nothing is written ---

    b.bits(1, 4)  # NumFillBits
    b.bits(0, 4)  # NumLineBits
    b.bits(1, 4)  # NumFillStyles
    b.bits(0, 4)  # NumLineStyles

    # StyleChangeRecord: move to (xmin, ymax) and select fill style 1.
    b.bit(0)  # TypeFlag = style change
    b.bit(0)  # StateNewStyles
    b.bit(0)  # StateLineStyle
    b.bit(1)  # StateFillStyle1
    b.bit(0)  # StateFillStyle0
    b.bit(1)  # StateMoveTo
    b.bits(12, 5)  # MoveBits
    b.sbits(xmin, 12)
    b.sbits(ymax, 12)
    b.bits(1, 1)  # FillStyle1 (SB[NumFillBits] = SB[1]) = style 1

    # Four straight edges closing the rectangle. Every one of them is encoded as
    # a *horizontal* edge (VertLineFlag clear), which carries DeltaY and infers
    # DeltaX = 0. Encoding the up/down moves as vertical edges would instead
    # carry them in DeltaX and land the shape nowhere near the intended box.
    ex = xmax - xmin
    ey = ymax - ymin
    coord_bits = 12  # ex/ey fit in a signed 12-bit field
    for delta in (ex, -ey, -ex, ey):
        b.bit(0)  # TypeFlag = edge
        b.bit(1)  # StraightFlag
        b.bit(0)  # GeneralLineFlag = false
        b.bit(0)  # VertLineFlag = false
        b.bits(coord_bits - 2, 4)  # NumBits field is (width - 2)
        b.sbits(delta, coord_bits)

    b.bit(0)  # EndShapeRecord TypeFlag
    b.bit(0)  # StraightFlag
    out += b.data()
    return tag_bytes(22, bytes(out))


def place_object2(char_id, depth, mtx=None, name=None):
    """Place char_id at `depth`, optionally giving the instance a name.

    The name is what C++ later addresses the object by (e.g. "key0" /
    "icon0"), so every HUD part must be placed with a name.
    """
    body = BitW()
    flags = 0x06  # HasCharacter | HasMatrix
    if name:
        flags |= 0x20  # HasName
    body.bits(flags, 8)
    body.bits(depth, 16)
    body.bits(char_id, 16)
    if mtx is None:
        mtx = matrix()
    # Everything after the flag byte is byte-aligned (UI16, UI16, a self
    # contained matrix, then a NUL-terminated name), so the matrix and name can
    # simply be concatenated rather than bit-packed.
    out = body.data() + mtx
    if name:
        out += name.encode("ascii") + b"\x00"
    return tag_bytes(26, out)


# Flash 8 DefineEditText flag bits (MSB first). These are 16 individual UB[1]
# fields, not one 16-bit word -- but writing them MSB first as a single word
# produces the identical byte sequence.
_H_HASTEXT = 0x8000
_H_WORDWRAP = 0x4000
_H_MULTILINE = 0x2000
_H_READONLY = 0x0800
_H_HASTEXTCOLOR = 0x0400
_H_HASFONT = 0x0100
_H_HASLAYOUT = 0x0020
_H_NOSELECT = 0x0010
_H_USEOUTLINES = 0x0001

# Flash's on-disk Align values. The published SWF spec text claims 1=center and
# 2=right, which is wrong: real files (and FFDec) use 0=left 1=right 2=center
# 3=justify.
ALIGN_LEFT, ALIGN_RIGHT, ALIGN_CENTER, ALIGN_JUSTIFY = 0, 1, 2, 3


def define_edit_text(char_id, bounds, text, color=(255, 235, 180, 255),
                     align=ALIGN_CENTER):
    """A read-only, non-interactive, dynamic text field.

    Deliberately carries NO font: HasFont and HasFontClass stay clear so no
    DefineFont2 and no imported font resource is needed. Scaleform then renders
    the text with the default face, which keeps this SWF completely
    self-contained -- it embeds no font, and pulls in no third-party asset.

    The field layout that follows the flags is fixed by the Flash 8 format and
    is easy to get wrong: FontId/FontHeight only appear when a font is set,
    HasLayout drags in five extra fields, and VariableName is ALWAYS present
    even when empty.
    """
    xmin, xmax, ymin, ymax = bounds
    flags = (_H_HASTEXT | _H_READONLY | _H_HASTEXTCOLOR | _H_HASLAYOUT | _H_NOSELECT)

    out = bytearray()
    out += struct.pack("<H", char_id)
    out += rect([xmin, xmax, ymin, ymax])
    out += struct.pack(">H", flags)
    # HasFont / HasFontClass clear -> no FontId, no FontHeight.
    out += bytes((color[0], color[1], color[2], color[3]))
    # HasLayout block
    out += struct.pack("<B", align)
    out += struct.pack("<H", 0)  # leftMargin
    out += struct.pack("<H", 0)  # rightMargin
    out += struct.pack("<H", 0)  # indent
    out += struct.pack("<h", 0)  # leading (SI16)
    out += b"\x00"               # VariableName, always present
    out += text.encode("utf-8") + b"\x00"
    return tag_bytes(37, bytes(out))


def define_empty_sprite(sprite_id):
    """A 1-frame, empty MovieClip.

    It exists to be named and addressed from C++: the plugin calls
    MovieClip.loadMovie() on it to pull a runtime icon sheet in. A plain
    DefineShape could not receive loaded content.
    """
    return define_sprite(sprite_id, 1, [show_frame(), end_tag()])


def show_frame():
    return tag_bytes(1)


def end_tag():
    return tag_bytes(0)


def define_sprite(sprite_id, frame_count, inner_tags):
    body = struct.pack("<HH", sprite_id, frame_count) + b"".join(inner_tags)
    return tag_bytes(39, body)


def export_assets(pairs):
    b = BitW()
    b.bits(len(pairs), 16)
    for tag_id, name in pairs:
        b.bits(tag_id, 16)
        b.bytes(name.encode("ascii") + b"\x00")
    return tag_bytes(56, b.data())


def save(path, w, h, tags, fps=30, frames=1):
    head = bytearray(header(w, h, fps, frames))
    payload = b"".join(tags)
    size = len(head) + len(payload)
    head[4:8] = struct.pack("<I", size)
    with open(path, "wb") as f:
        f.write(bytes(head) + payload)
    return size
