#!/usr/bin/env python3
"""mkgfx_f18a.py -- the VGA build's art (gfx/vga/*.png, tools/vgapal.py) for
the NABU's F18A (and PICO9918): Graphics I with ECM3 tiles and sprites.

    python3 tools/mkgfx_f18a.py OUTDIR [--preview DIR]
        # writes OUTDIR/gfxf18a.c, gfxf18a.h (src/nabu/gfx.c's F18A path)

ECM3: a pixel is 3 bits, one from each of three pattern planes (the
tiles' at the pattern table +0, +800h, +1000h; the sprites' the same from
theirs). A tile's attribute byte (the colour table, one a pattern) and a
sprite's colour byte pick one of 8 palette groups (ps << 1): its 8 colours
are palette entries ps*8 .. ps*8+7, 12-bit RGB. A sprite's 0 is clear.

The 64 colours, for two sets of screens (the arena; the menus):
  groups 0-3  the seats: 0 the screen's background, then the bombers'
              colours (VGA's 15 codes merged to 7, the same for every seat)
  groups 4-7  fitted to that screen's art (the arena's tiles; the logo and
              the trophy), each cell its best group of all 8
Text is drawn at run time from the font in the seat groups (a shade a
glyph line, VGA's gradient letters), on entry 0.

VGA cycles the fire's colours; here each animation phase is the tile drawn
in that phase's fire colours (the phases of src/nabu/gfx.c's anim_step).

Cells are 25 bytes: plane 0, 1, 2 (8 bytes each), then the attribute.
"""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from artlayout import TILES, SPRITES  # noqa: E402
from png import Image  # noqa: E402
import vgapal as V  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
VGA = os.path.join(ROOT, "gfx", "vga")
POWERUPS = ("fire", "bombup", "speed", "kick", "glove")
ANIMATED = ["burn", "bomb", "dud", "flameC", "flameH", "flameV", "flameL", "flameR", "flameU", "flameD",
            "fire", "bombup", "speed", "kick", "glove"]

# VGA's seat codes -> the 7 sprite colours: outline; the body dark, mid,
# light; the visor and boots; the visor's shine and the eyes; the buckle
CODE_SLOT = [0, 1, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 5, 5, 7]
# text: the slot each glyph line is drawn in (light at the top)
TEXT_LINES = [4, 4, 4, 3, 3, 2, 2, 2]

ARENA_BG = (22, 30, 56)         # the HUD's bar and the side panels
MENU_BG = (16, 20, 66)          # the menus' backdrop

PAL = V.palette()


def q4(c):
    return tuple(min(15, max(0, round(v / 17))) * 17 for v in c)


def dist(a, b):
    return 2 * (a[0] - b[0]) ** 2 + 4 * (a[1] - b[1]) ** 2 + 3 * (a[2] - b[2]) ** 2


def load(name):
    img = Image(os.path.join(VGA, name))
    assert img.idx is not None, name
    return img


def block(img, x0, y0, w, h):
    return [[img.idx[y0 + y][x0 + x] for x in range(w)] for y in range(h)]


def rgb_of(px, pal):
    return [[pal[p] for p in row] for row in px]


def cells8(px):
    """A 16x16 pixel block -> its 4 cells TL, TR, BL, BR (64 colours each)."""
    out = []
    for cy in (0, 8):
        for cx in (0, 8):
            out.append(tuple(px[cy + y][cx + x] for y in range(8) for x in range(8)))
    return out


# ---- the seats ----------------------------------------------------------------

def seat_groups(bg):
    """Groups 0-3: each seat's 7 colours (the codes merged by CODE_SLOT,
    averaged by how many pixels use them), entry 0 the background."""
    spr = load("sprites.png")
    count = [0] * 16
    for y in range(16):
        for x in range(spr.width):
            p = spr.idx[y][x]
            if p:
                count[p - V.SEAT] += 1
    groups = []
    for s in range(4):
        g = [q4(bg)]
        for slot in range(1, 8):
            cs = [(PAL[V.SEAT + 16 * s + c], max(1, count[c])) for c in range(1, 16) if CODE_SLOT[c] == slot]
            n = sum(w for _, w in cs)
            g.append(q4(tuple(sum(c[i] * w for c, w in cs) / n for i in range(3))))
        groups.append(g)
    return groups


def sprite_codes(name, w, h, nframes):
    img = load(name)
    out = []
    for f in range(nframes):
        px = block(img, f * w, 0, w, h)
        out.append([[0 if p == 0 else CODE_SLOT[p - V.SEAT] for p in row] for row in px])
    return out


# ---- fitting groups 4-7 ----------------------------------------------------------

def kmeans(pts, init, fixed0, it=10):
    """Lloyd over weighted colours pts {colour: weight}, from init (8);
    entry 0 stays put if fixed0."""
    cs = list(init)
    for _ in range(it):
        acc = [[0.0, 0.0, 0.0, 0.0] for _ in cs]
        for p, w in pts.items():
            j = min(range(8), key=lambda j: dist(p, cs[j]))
            a = acc[j]
            a[0] += p[0] * w
            a[1] += p[1] * w
            a[2] += p[2] * w
            a[3] += w
        new = []
        for j, a in enumerate(acc):
            if (j == 0 and fixed0) or not a[3]:
                new.append(cs[j])
            else:
                new.append(q4((a[0] / a[3], a[1] / a[3], a[2] / a[3])))
        if new == cs:
            break
        cs = new
    return cs


def cell_err(cell, g):
    return sum(min(dist(p, c) for c in g) for p in cell)


def fit(cells, weights, fixed, nfree, bg0):
    """Groups fixed + nfree fitted ones (entry 0 = bg0 when given) for the
    cells: each cell takes its best group. Returns the groups."""
    rnd = random.Random(7)
    # start: the cells the fixed groups serve worst, spread by their mean colour
    worst = sorted(range(len(cells)), key=lambda i: -min(cell_err(cells[i], g) for g in fixed) * weights[i])
    seeds = worst[:max(nfree * 6, 1)]
    rnd.shuffle(seeds)
    free = []
    for k in range(nfree):
        pts = {}
        for i in seeds[k::nfree]:
            for p in cells[i]:
                pts[p] = pts.get(p, 0) + weights[i]
        u = sorted(pts, key=lambda p: -pts[p])
        init = ([q4(bg0)] if bg0 else []) + u[:8]
        init += [init[-1]] * (8 - len(init))
        free.append(kmeans(pts, init[:8], bool(bg0)))
    for rnd_i in range(8):
        groups = fixed + free
        pick = [min(range(len(groups)), key=lambda g: cell_err(c, groups[g])) for c in cells]
        tot = sum(cell_err(c, groups[g]) * w for c, g, w in zip(cells, pick, weights))
        print(f"  round {rnd_i}: error {tot / sum(weights) / 64:.0f} a pixel", file=sys.stderr)
        new = []
        for k in range(nfree):
            pts = {}
            for c, g, w in zip(cells, pick, weights):
                if g == len(fixed) + k:
                    for p in c:
                        pts[p] = pts.get(p, 0) + w
            new.append(kmeans(pts, free[k], bool(bg0)) if pts else free[k])
        if new == free:
            break
        free = new
    return fixed + free


def encode(cell, groups):
    """A cell's 64 colours -> 25 bytes in its best group."""
    g = min(range(8), key=lambda k: cell_err(cell, groups[k]))
    slots = [min(range(8), key=lambda j: dist(p, groups[g][j])) for p in cell]
    planes = []
    for b in range(3):
        for y in range(8):
            v = 0
            for x in range(8):
                v = v << 1 | (slots[y * 8 + x] >> b & 1)
            planes.append(v)
    return planes + [g << 1]


def decode(enc, groups):
    g = groups[enc[24] >> 1]
    out = []
    for y in range(8):
        for x in range(8):
            s = sum((enc[b * 8 + y] >> (7 - x) & 1) << b for b in range(3))
            out.append(g[s])
    return out


def sprite_planes(codes, x0=0, y0=0):
    """16x16 of slots -> 96 bytes: each plane as the VDP wants a 16x16
    pattern (TL, BL, TR, BR)."""
    out = []
    for b in range(3):
        for qx in (0, 1):
            for qy in (0, 1):
                for y in range(8):
                    v = 0
                    for x in range(8):
                        v = v << 1 | (codes[y0 + qy * 8 + y][x0 + qx * 8 + x] >> b & 1)
                    out.append(v)
    return out


# ---- the art ----------------------------------------------------------------------

def tile_frames():
    """Each tile's 4 phases as RGB 16x16 (fire in that phase's colours)."""
    img = load("tiles.png")
    out = {}
    for i, name in enumerate(TILES):
        frames = []
        for f in range(4):
            px = block(img, f * 16, i * 16, 16, 16)
            if f and not any(p for r in px for p in r):
                break
            frames.append(px)
        n = len(frames)
        order = ([0, 0, 0, 0] if n == 1 else
                 ([0, 0, 1, 1] if name in POWERUPS else [0, 1, 0, 1]) if n == 2 else [0, 1, 2, 3])
        phases = []
        for p in range(4):
            pal = list(PAL)
            pal[V.FIRE:V.FIRE + 16] = V.fire_phase(p)
            phases.append(rgb_of(frames[order[p]], pal))
        out[name] = phases
    return out


def carray(decl, rows):
    out = [f"const uint8_t {decl} = {{"]
    for r in rows:
        out.append("    { " + ", ".join(f"0x{b:02X}" for b in r) + " },")
    out.append("};")
    return "\n".join(out)


def palette_words(groups):
    w = []
    for g in groups:
        for c in g:
            r, gg, b = (v // 17 for v in c)
            w.append(r << 8 | gg << 4 | b)
    return w


def main():
    args = sys.argv[1:]
    preview = None
    if "--preview" in args:
        i = args.index("--preview")
        preview = args[i + 1]
        del args[i:i + 2]
    out = args[0] if args else os.path.join(ROOT, "gen", "nabu")
    os.makedirs(out, exist_ok=True)

    tiles = tile_frames()
    cimg = load("cells.png")
    wall = tuple(PAL[p] for row in block(cimg, 0, 0, 8, 8) for p in row)

    # ---- the arena's cells, as src/nabu/gfx.c numbers them
    A_seat = seat_groups(ARENA_BG)
    arena = []                          # RGB cells, by code
    panel = tuple([q4(ARENA_BG)] * 64)
    arena.append(panel)                 # 0 CH_PANEL (entry 0 of group 0)
    arena.append(wall)                  # 1 CH_WALL
    floor = cells8(tiles["floor"][0])
    F_FLOOR = len(arena)
    arena += floor                      # 4 floor cells: TL, TR, BL, BR
    tile_chars = {}
    tile_chars["floor"] = [F_FLOOR + j for j in range(4)]
    tile_chars["trophy"] = [0] * 4

    def add(c):
        if c in arena:
            return arena.index(c)
        arena.append(c)
        return len(arena) - 1

    for name in TILES:
        if name not in ANIMATED and name not in tile_chars:
            tile_chars[name] = [add(c) for c in cells8(tiles[name][0])]
    anim_base = len(arena)
    anim = []                           # [kind][phase][4 cells]
    for k, name in enumerate(ANIMATED):
        per = [cells8(tiles[name][p]) for p in range(4)]
        anim.append(per)
        tile_chars[name] = [anim_base + k * 4 + j for j in range(4)]
        arena += per[0]
    n_arena = len(arena)

    # weights: how much of the screen a cell covers
    def weight_of(code):
        if code in range(F_FLOOR, F_FLOOR + 4):
            return 40
        if code in tile_chars["pillar"] or code in tile_chars["block"] or code == 1:
            return 12
        return 2
    fit_cells, fit_w = [], []
    for i, c in enumerate(arena):
        if i:
            fit_cells.append(c)
            fit_w.append(weight_of(i))
    for k in range(len(ANIMATED)):
        for p in range(1, 4):
            for c in anim[k][p]:
                fit_cells.append(c)
                fit_w.append(2)
    print("fitting the arena's groups", file=sys.stderr)
    A = fit(fit_cells, fit_w, A_seat, 4, None)

    enc_arena = [encode(c, A) for c in arena]
    enc_arena[0] = [0] * 24 + [0]       # the panel: entry 0 of group 0, exactly
    enc_anim = []                       # [kind][phase] -> 96 plane bytes (4 codes a plane) + 4 attributes
    for k in range(len(ANIMATED)):
        per = []
        for p in range(4):
            e = [encode(c, A) for c in anim[k][p]]
            planes = []
            for b in range(3):
                for j in range(4):
                    planes += e[j][b * 8:b * 8 + 8]
            per.append(planes + [x[24] for x in e])
        enc_anim.append(per)

    # ---- the menus: the logo and the trophy, on the backdrop
    M_seat = seat_groups(MENU_BG)
    limg = load("logo.png")
    lw = limg.width // 8
    lpal = list(PAL)
    lpal[0] = q4(MENU_BG)
    lpx = rgb_of(block(limg, 0, 0, limg.width, 24), lpal)
    lcells_rgb, lmap = [], []
    for cy in range(3):
        row = []
        for cx in range(lw):
            c = tuple(lpx[cy * 8 + y][cx * 8 + x] for y in range(8) for x in range(8))
            if c not in lcells_rgb:
                lcells_rgb.append(c)
            row.append(lcells_rgb.index(c))
        lmap.append(row)
    tpx = rgb_of(block(load("trophy.png"), 0, 0, 16, 16), lpal)
    trophy_rgb = cells8(tpx)
    print("fitting the menus' groups", file=sys.stderr)
    M = fit(lcells_rgb + trophy_rgb, [3] * len(lcells_rgb) + [4] * 4, M_seat, 4, MENU_BG)
    enc_logo = [encode(c, M) for c in lcells_rgb]
    enc_trophy = [encode(c, M) for c in trophy_rgb]

    # ---- sprites
    spr = sprite_codes("sprites.png", 16, 16, len(SPRITES))
    big = sprite_codes("bigwin.png", 32, 32, 2)
    enc_spr = [sprite_planes(f) for f in spr]
    enc_big = [[sprite_planes(f, qx * 16, qy * 16) for qy in (0, 1) for qx in (0, 1)] for f in big]

    font = open(os.path.join(ROOT, "gfx", "namco.bin"), "rb").read()
    assert len(font) == 768

    # ---- out
    h = ["/* GENERATED by tools/mkgfx_f18a.py from gfx/vga/*.png and tools/vgapal.py: do not edit. */",
         "#ifndef GFXF18A_H", "#define GFXF18A_H", "#include <stdint.h>", "",
         f"#define F_ARENA_CELLS {n_arena}",
         f"#define F_ANIM_BASE   {anim_base}",
         f"#define F_FLOOR       {F_FLOOR}       /* 4 cells: TL, TR, BL, BR */",
         f"#define F_LOGO_W      {lw}",
         f"#define F_LOGO_CELLS  {len(lcells_rgb)}",
         f"#define F_TEXT_LINES  {{ {', '.join(str(s) for s in TEXT_LINES)} }}",
         "",
         "extern const uint16_t f_pal_arena[64];     /* 0RGB */",
         "extern const uint16_t f_pal_menu[64];",
         "extern const uint8_t f_arena_cells[F_ARENA_CELLS][25];  /* planes 0-2, the attribute */",
         "extern const uint8_t f_anim[N_ANIM][4][100];  /* a kind's 4 codes at a phase: each plane's 32 bytes, 4 attributes */",
         "extern const uint8_t f_tile_chars[N_TILEKINDS][4];",
         "extern const uint8_t f_logo_cells[F_LOGO_CELLS][25];",
         "extern const uint8_t f_logo_map[3][F_LOGO_W];",
         "extern const uint8_t f_trophy_cells[4][25];",
         "extern const uint8_t f_spr[N_FRAMES][96];   /* 16x16, planes 0-2 */",
         "extern const uint8_t f_big[2][4][96];",
         "", "#endif", ""]
    open(os.path.join(out, "gfxf18a.h"), "w").write("\n".join(h))

    def words(decl, ws):
        return "\n".join([f"const uint16_t {decl} = {{"] +
                         ["    " + ", ".join(f"0x{v:03X}" for v in ws[i:i + 8]) + "," for i in range(0, 64, 8)] + ["};"])

    c = ["/* GENERATED by tools/mkgfx_f18a.py from gfx/vga/*.png and tools/vgapal.py: do not edit. */",
         "#include <stdint.h>", '#include "gfxcodes.h"', '#include "gfxf18a.h"', "",
         words("f_pal_arena[64]", palette_words(A)), "",
         words("f_pal_menu[64]", palette_words(M)), "",
         carray("f_arena_cells[F_ARENA_CELLS][25]", enc_arena), ""]
    c.append("const uint8_t f_anim[N_ANIM][4][100] = {")
    for per in enc_anim:
        c.append("  {")
        for ph in per:
            c.append("    { " + ", ".join(f"0x{b:02X}" for b in ph) + " },")
        c.append("  },")
    c.append("};")
    c += ["", carray("f_tile_chars[N_TILEKINDS][4]", [tile_chars[n] for n in TILES]), "",
          carray("f_logo_cells[F_LOGO_CELLS][25]", enc_logo), "",
          carray("f_logo_map[3][F_LOGO_W]", lmap), "",
          carray("f_trophy_cells[4][25]", enc_trophy), "",
          carray("f_spr[N_FRAMES][96]", enc_spr), ""]
    c.append("const uint8_t f_big[2][4][96] = {")
    for f in enc_big:
        c.append("  {")
        for q in f:
            c.append("    { " + ", ".join(f"0x{b:02X}" for b in q) + " },")
        c.append("  },")
    c.append("};")
    c.append("")
    open(os.path.join(out, "gfxf18a.c"), "w").write("\n".join(c))
    size = 128 + 128 + n_arena * 25 + len(ANIMATED) * 400 + len(lcells_rgb) * 25 + 100 + len(SPRITES) * 96 + 768
    print(f"{out}: {n_arena} arena cells, {len(lcells_rgb)} logo cells, about {size} bytes")

    if preview:
        make_preview(preview, A, M, enc_arena, enc_anim, tile_chars, F_FLOOR, enc_logo, lmap,
                     enc_trophy, spr, big)


def make_preview(d, A, M, enc_arena, enc_anim, tile_chars, F_FLOOR, enc_logo, lmap, enc_trophy, spr, big):
    """Mock screens (256x192, the NABU's), drawn from the encoded data."""
    from PIL import Image as PI
    os.makedirs(d, exist_ok=True)

    def put(img, cx, cy, enc, groups):
        for i, c in enumerate(decode(enc, groups)):
            img.putpixel((cx * 8 + i % 8, cy * 8 + i // 8), c)

    def sprite(img, x, y, codes, seat, groups, scale=1):
        for yy, row in enumerate(codes):
            for xx, s in enumerate(row):
                if s:
                    img.putpixel((x + xx, y + yy), groups[seat][s])

    def text(img, cx, cy, s, seat, groups):
        font = open(os.path.join(ROOT, "gfx", "namco.bin"), "rb").read()
        for k, ch in enumerate(s):
            g = ord(ch) - 32
            for y in range(8):
                b = font[g * 8 + y]
                for x in range(8):
                    img.putpixel(((cx + k) * 8 + x, cy * 8 + y),
                                 groups[seat][TEXT_LINES[y]] if b >> (7 - x) & 1 else groups[seat][0])

    img = PI.new("RGB", (256, 192), A[0][0])
    rnd = random.Random(3)
    names = TILES
    for r in range(24):
        for col in range(32):
            if r == 23 and col >= 4 or r and (col == 4 or col == 31):
                put(img, col, r, enc_arena[1], A)
            elif r and col >= 5:
                tr, tc = (r - 1) // 2, (col - 5) // 2
                j = ((r - 1) & 1) * 2 + ((col - 5) & 1)
                t = "floor"
                if tr % 2 and tc % 2:
                    t = "pillar"
                elif (tr * 7 + tc * 3) % 5 == 0 and tr + tc > 2:
                    t = "block"
                if (tr, tc) in ((5, 2), (5, 3), (5, 4), (4, 3), (6, 3)):
                    t = {(5, 2): "flameL", (5, 4): "flameR", (4, 3): "flameU", (6, 3): "flameD"}.get((tr, tc), "flameC")
                if (tr, tc) == (8, 6):
                    t = "bomb"
                if (tr, tc) == (2, 8):
                    t = "speed"
                if (tr, tc) == (2, 10):
                    t = "fire"
                code = tile_chars[t][j]
                if t in ANIMATED:
                    k = ANIMATED.index(t)
                    e = enc_anim[k][1]
                    enc = [e[b * 32 + j * 8 + y] for b in range(3) for y in range(8)] + [e[96 + j]]
                else:
                    enc = enc_arena[code]
                put(img, col, r, enc, A)
    for s, (x, y) in enumerate(((48, 16), (200, 40), (120, 120), (210, 160))):
        sprite(img, x, y, spr[s * 3 % 12], s, A)
    for s in range(4):
        text(img, 4 + s * 7, 0, ["ANN1", "BOB0", "CAT2", "DAN1"][s], s, A)
    for i, t in enumerate(("fire", "bombup", "speed")):
        for j in range(4):
            put(img, j & 1, 3 + i * 4 + (j >> 1), enc_arena[tile_chars[t][j]], A)
        text(img, 2, 4 + i * 4, str(i + 2), 0, A)
    img.resize((768, 576), PI.NEAREST).save(os.path.join(d, "f18a-arena.png"))

    img = PI.new("RGB", (256, 192), M[0][0])
    x0 = (32 - len(lmap[0])) // 2
    for r in range(3):
        for i, k in enumerate(lmap[r]):
            put(img, x0 + i, 3 + r, enc_logo[k], M)
    for j in range(4):
        put(img, 15 + (j & 1), 8 + (j >> 1), enc_trophy[j], M)
    text(img, 4, 12, "BOMBS AND BLASTS FOR 2-4", 0, M)
    text(img, 9, 14, "PRESS START", 3, M)
    text(img, 9, 16, "ONLINE PLAY", 2, M)
    text(img, 11, 18, "OPTIONS", 1, M)
    for s in range(4):
        sprite(img, 40 + s * 48, 160, spr[s % 3], s, M)
    for q in range(4):
        sprite(img, 200 + (q & 1) * 16, 64 + (q >> 1) * 16,
               [row[(q & 1) * 16:(q & 1) * 16 + 16] for row in big[0][(q >> 1) * 16:(q >> 1) * 16 + 16]], 2, M)
    img.resize((768, 576), PI.NEAREST).save(os.path.join(d, "f18a-menu.png"))


if __name__ == "__main__":
    main()
