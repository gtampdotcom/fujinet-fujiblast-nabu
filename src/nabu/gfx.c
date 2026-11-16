/*
 * gfx.c -- the Adam's screens (gfx.h): the TMS9918A in Graphics II used as
 * a character map. The screen is 32 x 24 cells, each its own pattern and
 * colours (two colours in a line of 8 pixels); each third of the screen
 * has its own 256 characters, loaded alike in all three.
 *
 * The game's 40-column, 25-row screens are mapped on: row r is row r-1 (the
 * arena has its own layout), a text column c is c-2, a BIG row (20 big
 * characters on the Atari) is 20 cells from column 6, and a sprite's x is
 * x * 3/2, its y 8 less.
 *
 * The arena: row 0 the HUD, rows 1-22 the arena (13 x 11 tiles of 2 x 2
 * cells from column 5), row 23 and columns 4 and 31 its wall, columns 0-3
 * the player's stats and the round. A tile's 4 cells are characters fixed
 * for the screen (gfxcodes.h); the tiles that animate have their characters
 * redrawn in the frame's blank, one or two a frame (anim_step), and only the
 * kinds on screen (anim_use): redrawing all 15 every phase took about half
 * the Z80's time.
 *
 * Text is the Namco font, a cell a character in the colour wanted: a
 * character for each (glyph, colours) in use is made when first drawn, and
 * forgotten with the screen (glyph_code).
 *
 * Each bomber is two 16 x 16 sprites (the VDP's sprites have a colour
 * each): A in the seat's colour, B in black in front of it with the eyes
 * cut out. Four sprites fit on a line: when 3 bombers share lines they
 * take turns at the front.
 *
 * On an F18A (or a PICO9918) the screens are the VGA build's art instead
 * (tools/mkgfx_f18a.py): Graphics I with ECM3 tiles and sprites, 3 bits a
 * pixel from three pattern planes, each pattern's attribute (and each
 * sprite's colour byte) one of 8 palette groups of 8 colours. The codes and
 * the drawing are the same as above, with the F18A's cells (f_*): text a
 * shade a glyph line in the seat groups, a bomber one 8-colour sprite. The
 * F18A is unlocked once (f18a_detect) and not locked again till we quit
 * (gfx_quit): a lock (any VR57 write after) turns the ECM3 sprites back
 * into outlines.
 *
 * SYM switches the two at any time (gfx_toggle, from input.c): the codes
 * are laid out alike in both (the arena's cells and the logo get room for
 * the larger of the two), so the screen's name table, kept in RAM as it is
 * written (nt: no reading the VRAM back), stays good; the characters in use
 * are made again the other way, and the arena's tiles and stats redrawn.
 */
#ifdef __NABU__

#include <string.h>
#include "gfx.h"
#include "plat.h"
#include "proto.h"
#include "gfxcodes.h"
#include "nabu.h"
#include "gfxf18a.h"

#define R1_OFF  0xA2            /* 16K, the interrupt, 16 x 16 sprites; display off */
#define R1_ON   0xE2
#define TEXT_SHIFT 2
#define BIG_COL    6
#define ARENA_COL  5            /* the arena's first cell column */
#define NT(row, col)  (VR_NAME + (row) * 32 + (col))

/* colours (the TMS9918A's) */
#define BLACK 1
#define BLUE  4
#define WHITE 15
static const uint8_t seat_colour[SEATS] = { 15, 8, 7, 11 };

/* palettes: background, then the 4 colours text asks for */
static const uint8_t pal[][5] = {
    { BLUE, 15, 11, 7, 9 },     /* MENU */
    { BLUE, 15, 11, 7, 9 },     /* GFX */
    { BLACK, 15, 8, 7, 11 },    /* HUD: the seats' colours */
    { BLACK, 15, 8, 7, 11 },    /* ARENA */
    { BLUE, 15, 15, 15, 15 },   /* SEAT0..3: text in the seat's colour */
    { BLUE, 8, 8, 8, 15 },
    { BLUE, 7, 7, 7, 15 },
    { BLUE, 11, 11, 11, 15 },
    { BLUE, 15, 11, 7, 9 },     /* TITLE */
};

/* the F18A: Graphics I, ECM3 (f18a: drawing that way; f18a_found: there
 * is one, so the title offers the choice, gfx_set_f18a) */
uint8_t f18a, f18a_found;
#define FV_PLANE 0x0800         /* a pattern plane from the one before */
#define FV_ATTR  0x1C00         /* the tiles' attributes: a byte a pattern */
#define FV_SPAT  0x2000         /* sprite patterns, planes at +0, +800h, +1000h */
static const uint8_t (*tchars)[4] = tile_chars;
static uint8_t anim_base = ANIM_BASE;
static const uint8_t text_lines[8] = F_TEXT_LINES;

static uint8_t row_kind[SCR_ROWS], row_pal[SCR_ROWS];
static uint8_t scr_bg = BLUE;
static uint8_t blanked;                 /* the display is off: the VDP is ours */
static uint8_t arena_on;
static uint8_t nt[768];                 /* the name table as written */

/* both ways' codes alike: room for the larger */
#define AR_CODES   (F_ARENA_CELLS > ARENA_CELLS ? F_ARENA_CELLS : ARENA_CELLS)
#define LOGO_CODES (F_LOGO_CELLS > LOGO_CELLS ? F_LOGO_CELLS : LOGO_CELLS)

/* ---- the VRAM ------------------------------------------------------------ */

static void vwrite(uint16_t addr, const uint8_t *src, uint16_t n)
{
    if (addr >= VR_NAME && addr < VR_NAME + 768)   /* (writes don't cross it) */
        memcpy(nt + (addr - VR_NAME), src, n);
    va_addr = addr;
    va_src = src;
    va_len = n;
    if (blanked)
        va_writef();
    else
        va_write();
}

static void vfill(uint16_t addr, uint8_t v, uint16_t n)
{
    uint16_t a = addr < VR_NAME ? VR_NAME : addr, e = addr + n;
    if (e > VR_NAME + 768 || e < addr)
        e = VR_NAME + 768;
    if (a < e)
        memset(nt + (a - VR_NAME), v, e - a);
    va_addr = addr;
    va_val = v;
    va_len = n;
    va_fill();
}

static void vreg(uint8_t r, uint8_t v)
{
    VDP_LOCK();
    VDP_CTRL = v;
    VDP_CTRL = 0x80 | r;
    VDP_UNLOCK();
}

/* A cell (8 pattern bytes, 8 colours) as character code, in all 3 thirds. */
static void put_cell(uint8_t code, const uint8_t *cell, uint8_t fast)
{
    uint8_t t, was = blanked;
    uint16_t a;
    blanked = fast;
    for (t = 0; t < 3; ++t) {
        a = (uint16_t)code * 8 + t * 0x800;
        vwrite(VR_PAT + a, cell, 8);
        vwrite(VR_COL + a, cell + 8, 8);
    }
    blanked = was;
}

/* The F18A's: a cell (3 planes of 8 bytes, the attribute) as code. */
static void put_fcell(uint8_t code, const uint8_t *cell, uint8_t fast)
{
    uint8_t b, was = blanked;
    uint16_t a = (uint16_t)code * 8;
    blanked = fast;
    for (b = 0; b < 3; ++b)
        vwrite(VR_PAT + a + b * FV_PLANE, cell + b * 8, 8);
    vwrite(FV_ATTR + code, cell + 24, 1);
    blanked = was;
}

/* The TMS9918A's 16 colours as the F18A has them at power-on: put back
 * for the stock screens on an F18A. */
static const uint16_t tms_pal[16] = {
    0x000, 0x000, 0x2C3, 0x5D6, 0x54F, 0x76F, 0xD54, 0x4EF,
    0xF54, 0xF76, 0xDC3, 0xED6, 0x2B2, 0xC5C, 0xCCC, 0xFFF,
};

/* n colours (0RGB) from entry 0, through the data port (VR47: DPM, auto increment). */
static void f_palette(const uint16_t *p, uint8_t n)
{
    uint8_t i;
    VDP_LOCK();
    VDP_CTRL = 0xC0;
    VDP_CTRL = 0x80 | 47;
    for (i = 0; i < n; ++i) {
        VDP_DATA = (uint8_t)(p[i] >> 8);
        VDP_DATA = (uint8_t)p[i];
    }
    VDP_CTRL = 0;
    VDP_CTRL = 0x80 | 47;
    VDP_UNLOCK();
}

/* An F18A or a PICO9918: unlocked (VR57 = 1Ch twice), it says so in status
 * register 1's top bits (VR15 picks it). A TMS9918A takes the writes as
 * registers 1 and 7, which gfx_init sets again. */
static uint8_t f18a_detect(void)
{
    uint8_t id;
    VDP_LOCK();
    id = VDP_CTRL;
    VDP_CTRL = 0x1C;
    VDP_CTRL = 0x80 | 57;
    VDP_CTRL = 0x1C;
    VDP_CTRL = 0x80 | 57;
    VDP_CTRL = 1;
    VDP_CTRL = 0x80 | 15;
    id = VDP_CTRL;              /* (the first may be stale) */
    id = VDP_CTRL;
    VDP_CTRL = 0;
    VDP_CTRL = 0x80 | 15;
    VDP_UNLOCK();
    return (id & 0xE0) == 0xE0;
}

/* A glyph's F18A cell: seat group by the TMS colour asked for, a shade a
 * line on entry 0; inverse (on a white band): dark on the orange group's light. */
static void f_glyph(uint8_t *cell, uint8_t g, uint8_t fg, uint8_t bg)
{
    uint8_t y, b, f, s, k, grp, inv = bg == WHITE;
    const uint8_t *src = font_gfx + g * 8;
    if (inv)
        grp = 3;
    else if (fg == 15 || fg == 14)
        grp = 0;
    else if (fg == 8 || fg == 9 || fg == 6 || fg == 13)
        grp = 1;
    else if (fg == 7 || fg == 4 || fg == 5)
        grp = 2;
    else
        grp = 3;
    for (y = 0; y < 8; ++y) {
        f = src[y];
        s = inv ? 1 : text_lines[y];
        k = inv ? 4 : 0;
        for (b = 0; b < 3; ++b)
            cell[b * 8 + y] = ((s >> b) & 1 ? f : 0) | ((k >> b) & 1 ? (uint8_t)~f : 0);
    }
    cell[24] = grp << 1;
}

/* ---- characters ----------------------------------------------------------- */

#define FC_MAX 200
static uint8_t next_code;               /* the next character free (0: none) */
static uint16_t fc_key[FC_MAX];
static uint8_t fc_code[FC_MAX], fc_n;
static uint8_t logo_code, trophy_code, logo_row;

/* glyph key (glyph << 8 | fg << 4 | bg) as character c, the way we draw now */
static void put_glyph(uint8_t c, uint16_t key)
{
    uint8_t cell[25], g = key >> 8, fg = (key >> 4) & 15, bg = key & 15;
    if (f18a) {
        f_glyph(cell, g, fg, bg);
        put_fcell(c, cell, blanked);
    } else {
        memcpy(cell, font_gfx + g * 8, 8);
        memset(cell + 8, fg << 4 | bg, 8);
        put_cell(c, cell, blanked);
    }
}

static uint8_t alloc_codes(uint8_t n)
{
    uint8_t c;
    if (!next_code || (uint16_t)next_code + n > 256)
        return 0;
    c = next_code;
    next_code += n;                     /* (256 wraps to 0: none left) */
    return c;
}

/* The character for a glyph (ASCII - 32) in colours fg on bg. */
static uint8_t glyph_code(uint8_t g, uint8_t fg, uint8_t bg)
{
    uint16_t key = (uint16_t)g << 8 | fg << 4 | bg;
    uint8_t i, c;
    if (!g && bg == scr_bg)
        return CH_PANEL;
    for (i = 0; i < fc_n; ++i)
        if (fc_key[i] == key)
            return fc_code[i];
    if (fc_n >= FC_MAX || !(c = alloc_codes(1)))
        return CH_PANEL;
    put_glyph(c, key);
    fc_key[fc_n] = key;
    fc_code[fc_n++] = c;
    return c;
}

static uint8_t glyph_of(char ch)
{
    uint8_t a = (uint8_t)ch;
    if (a >= 'a' && a <= 'z')
        a -= 32;
    return a >= 32 && a < 128 ? a - 32 : 0;
}

static uint8_t tms_row(uint8_t row)
{
    return row ? row - 1 : 0;
}

/* A screen's characters start again: code 0 the blank in the screen's colour. */
static void put_blank(uint8_t bg)
{
    uint8_t blank[25];
    if (f18a) {
        memset(blank, 0, 25);           /* entry 0 of group 0: the screen's background */
        f_palette(bg == BLACK ? f_pal_arena : f_pal_menu, 64);
        put_fcell(CH_PANEL, blank, 1);
    } else {
        memset(blank, 0, 8);
        memset(blank + 8, bg << 4 | bg, 8);
        put_cell(CH_PANEL, blank, 1);
    }
    vreg(7, f18a ? 0 : bg);
}

static void charset_reset(uint8_t bg)
{
    scr_bg = bg;
    put_blank(bg);
    next_code = 1;
    fc_n = 0;
    logo_code = trophy_code = 0;
    arena_on = 0;
}

/* ---- screens ---------------------------------------------------------------- */

static uint8_t sat[64];                 /* the first 16 sprites' attributes */
static uint8_t son[SEATS], sbig[SEATS], sfr[SEATS], sseat[SEATS];
static uint8_t sx[SEATS], sy[SEATS];

static void gfx_setup(void);

void gfx_init(void)
{
    f18a = f18a_found = f18a_detect();
    gfx_setup();
}

/* The F18A's screens (on: 1) or the stock ones, from a blank screen. */
void gfx_set_f18a(uint8_t on)
{
    f18a = on && f18a_found;
    gfx_setup();
}

/* Quitting: the F18A as it comes up at power-on, for whatever runs next.
 * Its palette outlives a reset (only power-on reloads it), and the boot ROM
 * and stock software draw with entries 0-15: left as ours, their blue
 * comes out pink. So the TMS9918A's 16 colours back, ECM off, and the lock
 * on again (safe now: nothing of ours draws after). */
void gfx_quit(void)
{
    if (!f18a_found)
        return;
    vreg(1, R1_OFF);
    f_palette(tms_pal, 16);
    vreg(49, 0);
    vreg(57, 0);                /* (not 1Ch: locked) */
}

/* The VDP set for the way we draw now, its sprites' patterns loaded. */
static void vdp_mode(void)
{
    uint8_t f, q;
    static const uint8_t end[1] = { 0xD0 };
    vreg(1, R1_OFF);
    blanked = 1;
    if (f18a) {
        uint8_t b;
        vreg(0, 0x00);          /* Graphics I */
        vreg(1, R1_OFF);
        vreg(2, VR_NAME >> 10);
        vreg(3, FV_ATTR >> 6);
        vreg(4, 0);             /* patterns' planes at 0000h, 0800h, 1000h */
        vreg(5, VR_SAT >> 7);
        vreg(6, FV_SPAT >> 11);
        vreg(29, 0);            /* the planes 800h apart */
        vreg(49, 0x33);         /* ECM3: tiles and sprites */
        for (f = 0; f < N_FRAMES; ++f)
            for (b = 0; b < 3; ++b)
                vwrite(FV_SPAT + b * FV_PLANE + (uint16_t)f * 32, f_spr[f] + b * 32, 32);
        for (f = 0; f < 2; ++f)
            for (q = 0; q < 4; ++q)
                for (b = 0; b < 3; ++b)
                    vwrite(FV_SPAT + b * FV_PLANE + (uint16_t)(N_FRAMES + f * 4 + q) * 32, f_big[f][q] + b * 32, 32);
        tchars = f_tile_chars;
        anim_base = F_ANIM_BASE;
        vwrite(VR_SAT + 64, end, 1);
        return;
    }
    if (f18a_found) {           /* (still unlocked: never lock it again) */
        vreg(49, 0);            /* ECM off */
        f_palette(tms_pal, 16);
    }
    tchars = tile_chars;
    anim_base = ANIM_BASE;
    vreg(0, 0x02);              /* Graphics II */
    vreg(1, R1_OFF);
    vreg(2, VR_NAME >> 10);
    vreg(3, 0xFF);              /* colours at 2000h, patterns at 0000h, each third its own */
    vreg(4, 0x03);
    vreg(5, VR_SAT >> 7);
    vreg(6, VR_SPAT >> 11);
    for (f = 0; f < N_FRAMES; ++f) {
        vwrite(VR_SPAT + (uint16_t)f * 64, spr_b[f], 32);
        vwrite(VR_SPAT + (uint16_t)f * 64 + 32, spr_a[f], 32);
    }
    for (f = 0; f < 2; ++f)
        for (q = 0; q < 4; ++q) {
            vwrite(VR_SPAT + (uint16_t)(N_FRAMES * 2 + f * 8 + q) * 32, spr_bigB[f][q], 32);
            vwrite(VR_SPAT + (uint16_t)(N_FRAMES * 2 + f * 8 + 4 + q) * 32, spr_bigA[f][q], 32);
        }
    vwrite(VR_SAT + 64, end, 1);        /* the 17th sprite ends the list */
}

static void gfx_setup(void)
{
    vreg(1, R1_OFF);
    blanked = 1;
    vfill(0, 0, 0x4000);
    vdp_mode();
    memset(row_kind, ROW_TEXT, sizeof(row_kind));
    gfx_begin();
}

void gfx_begin(void)
{
    uint8_t r;
    frame_wait();
    vreg(1, R1_OFF);
    blanked = 1;
    spr_hide_all();
    gfx_fast = 1;
    spr_flush();
    charset_reset(BLUE);
    vfill(VR_NAME, 0, 768);
    for (r = 0; r < SCR_ROWS; ++r) {
        row_kind[r] = ROW_TEXT;
        row_pal[r] = PAL_MENU;
    }
}

void gfx_row(uint8_t row, uint8_t kind, uint8_t p)
{
    row_kind[row] = kind;
    row_pal[row] = p;
}

void gfx_show(void)
{
    frame_wait();
    vreg(1, R1_ON);
    blanked = 0;
}

void gfx_clear_row(uint8_t row)
{
    vfill(NT(tms_row(row), 0), CH_PANEL, 32);
}

/* The HUD's 20 characters are 5 groups of 4 (a seat's name and wins, the
 * timer): each group a cell apart from the next, across the screen. */
void gfx_big_line(uint8_t row, const char *s, const uint8_t *colour)
{
    uint8_t line[24], i, g;
    const uint8_t *p = pal[row_pal[row]];
    for (i = 0; i < 24; ++i)
        line[i] = CH_PANEL;
    for (i = 0; i < 20; ++i) {
        g = i >> 2;
        line[i + g] = glyph_code(glyph_of(s[i]), p[1 + (colour[i] & 3)], p[0]);
    }
    vwrite(NT(tms_row(row), 4), line, 24);
}

void gfx_text(uint8_t row, uint8_t col, const char *s, uint8_t colour)
{
    uint8_t line[32], n = 0, fg, bg, big = row_kind[row] == ROW_BIG;
    int16_t pos = big ? BIG_COL + col : (int16_t)col - TEXT_SHIFT, start = -1;
    const uint8_t *p = pal[row_pal[row]];
    bg = p[0];
    if (big) {
        fg = p[1 + (colour & 3)];
    } else if (colour) {
        fg = 8;                         /* inverse: on a white band */
        bg = WHITE;
    } else {
        fg = p[1];
    }
    for (; *s; ++s, ++pos) {
        if (pos < 0)
            continue;
        if (pos > 31)
            break;
        if (start < 0)
            start = pos;
        line[n++] = glyph_code(glyph_of(*s), fg, bg);
    }
    if (n)
        vwrite(NT(tms_row(row), (uint8_t)start), line, n);
}

void gfx_number(uint8_t row, uint8_t col, uint8_t n, uint8_t colour)
{
    char b[4], *p = b + 3;
    *p = 0;
    do {
        *--p = '0' + n % 10;
        n /= 10;
    } while (n);
    gfx_text(row, col, p, colour);
}

void gfx_logo(uint8_t row)
{
    uint8_t r, i, line[32], w = f18a ? F_LOGO_W : LOGO_W;
    if (!logo_code) {
        logo_code = alloc_codes(LOGO_CODES);
        if (logo_code) {
            if (f18a)
                for (i = 0; i < F_LOGO_CELLS; ++i)
                    put_fcell(logo_code + i, f_logo_cells[i], blanked);
            else
                for (i = 0; i < LOGO_CELLS; ++i)
                    put_cell(logo_code + i, logo_cells[i], blanked);
        }
    }
    logo_row = row;
    for (r = 0; r < 3; ++r) {
        gfx_row(row + r, ROW_GFX, PAL_TITLE);
        for (i = 0; i < w; ++i)
            line[i] = logo_code ? logo_code + (f18a ? f_logo_map[r][i] : logo_map[r][i]) : CH_PANEL;
        vwrite(NT(tms_row(row + r), (32 - w) / 2), line, w);
    }
}

void gfx_trophies(uint8_t row, uint8_t col, uint8_t n)
{
    uint8_t i, a[32], b[32], c = BIG_COL + col / 2;
    if (!trophy_code) {
        trophy_code = alloc_codes(4);
        if (trophy_code)
            for (i = 0; i < 4; ++i) {
                if (f18a)
                    put_fcell(trophy_code + i, f_trophy_cells[i], blanked);
                else
                    put_cell(trophy_code + i, trophy_cells[i], blanked);
            }
    }
    gfx_row(row, ROW_GFX, PAL_GFX);
    gfx_row(row + 1, ROW_GFX, PAL_GFX);
    if (!trophy_code)
        return;
    if (n > (32 - c) / 2)
        n = (32 - c) / 2;
    for (i = 0; i < n; ++i) {
        a[i * 2] = trophy_code;
        a[i * 2 + 1] = trophy_code + 1;
        b[i * 2] = trophy_code + 2;
        b[i * 2 + 1] = trophy_code + 3;
    }
    if (n) {
        vwrite(NT(tms_row(row), c), a, n * 2);
        vwrite(NT(tms_row(row + 1), c), b, n * 2);
    }
}

/* ---- the arena ----------------------------------------------------------------- */

static uint8_t anim_ph[N_ANIM], anim_next;
static uint8_t anim_use[N_ANIM];        /* tiles of each animated kind on screen */
static uint8_t at_tile[ARENA_W * ARENA_H];  /* the tile drawn at each place (255: none) */
static uint8_t stat_on[5];              /* the left panel shows powerup i's icon */
static uint16_t bombs_t;                /* frame_count when a moving bomb was last drawn */
static uint8_t anim4[N_ANIM][4][64];    /* each kind's phases: 4 patterns, then 4 colours, in a row */
static uint8_t anim4_made;

static uint8_t stat_n[5], stat_set, round_n, round_set;   /* (for gfx_toggle) */

static void anim_restart(void);

/* The arena's characters (codes 0 up), the way we draw now. */
static void arena_cells_load(void)
{
    uint8_t i;
    if (f18a) {
        for (i = 0; i < F_ARENA_CELLS; ++i)
            put_fcell(i, f_arena_cells[i], 1);
        return;
    }
    for (i = 0; i < ARENA_CELLS; ++i)
        put_cell(i, arena_cells[i], 1);
    if (!anim4_made) {
        uint8_t k, ph, j;
        for (k = 0; k < N_ANIM; ++k)
            for (ph = 0; ph < 4; ++ph)
                for (j = 0; j < 4; ++j) {
                    memcpy(anim4[k][ph] + j * 8, anim_cells[k][ph][j], 8);
                    memcpy(anim4[k][ph] + 32 + j * 8, anim_cells[k][ph][j] + 8, 8);
                }
        anim4_made = 1;
    }
}

/* An arena row's walls and floor (rows 1-22). */
static void floor_line(uint8_t *line, uint8_t r)
{
    uint8_t i;
    line[4] = line[31] = CH_WALL;
    if (f18a)                   /* the grass: 4 cells a tile */
        for (i = 5; i < 31; ++i)
            line[i] = F_FLOOR + ((r - 1) & 1) * 2 + ((i - 5) & 1);
    else
        memset(line + 5, CH_FLOOR, 26);
}

static void use_tile(uint8_t tile, int8_t d)
{
    uint8_t k;
    if (tile < N_TILEKINDS && (k = tile_anim_kind[tile]) < N_ANIM)
        anim_use[k] += d;
}

void gfx_arena(void)
{
    uint8_t r, line[32];
    charset_reset(BLACK);
    arena_cells_load();
    next_code = AR_CODES;
    memset(anim_ph, 0, sizeof(anim_ph));
    memset(anim_use, 0, sizeof(anim_use));
    anim_restart();
    memset(at_tile, 255, sizeof(at_tile));
    memset(stat_on, 0, sizeof(stat_on));
    stat_set = round_set = 0;
    anim_next = 0;
    arena_on = 1;
    gfx_row(0, ROW_BIG, PAL_HUD);
    for (r = 1; r < SCR_ROWS; ++r)
        gfx_row(r, ROW_ARENA, PAL_ARENA);
    for (r = 0; r < 24; ++r) {
        memset(line, CH_PANEL, 32);
        if (r == 23)
            memset(line + 4, CH_WALL, 28);
        else if (r)
            floor_line(line, r);
        vwrite(NT(r, 0), line, 32);
    }
}

static void put4(uint16_t at, const uint8_t *t)
{
    vwrite(at, t, 2);
    vwrite(at + 32, t + 2, 2);
}

void gfx_tile(uint8_t c, uint8_t r, uint8_t tile)
{
    uint8_t *at = at_tile + r * ARENA_W + c;
    if (*at != tile) {
        use_tile(*at, -1);
        use_tile(tile, 1);
        *at = tile;
    }
    put4(NT(1 + r * 2, ARENA_COL + c * 2), tchars[tile]);
}

void gfx_bomb_at(uint8_t hx, uint8_t hy, uint8_t dud)
{
    bombs_t = frame_count;      /* (a moving bomb: bombs and duds animate a while) */
    put4(NT(1 + hy, ARENA_COL + hx), tchars[dud ? T_DUD : T_BOMB]);
}

/* Left panel: powerup i's icon at rows 3+4i, its count beside it. */
void gfx_stat(uint8_t i, uint8_t n)
{
    uint16_t at = NT(3 + i * 4, 0);
    uint8_t d[2];
    static const uint8_t blank[2] = { CH_PANEL, CH_PANEL }, zero4[4] = { CH_PANEL, CH_PANEL, CH_PANEL, CH_PANEL };
    stat_n[i] = n;
    stat_set |= 1 << i;
    stat_on[i] = i < 3 || n;
    if (i >= 3 && !n) {
        put4(at, zero4);
        vwrite(at + 34, blank, 2);
        return;
    }
    put4(at, tchars[T_FIRE + i]);
    if (i < 3) {
        d[0] = n >= 10 ? glyph_code(16 + n / 10, WHITE, BLACK) : CH_PANEL;
        d[1] = glyph_code(16 + n % 10, WHITE, BLACK);
        vwrite(at + 32 + 2, d, 2);
    }
}

/* Right of the stats: RND and the round. */
void gfx_round(uint8_t round)
{
    uint8_t d[3];
    round_n = round;
    round_set = 1;
    d[0] = glyph_code(glyph_of('R'), WHITE, BLACK);
    d[1] = glyph_code(glyph_of('N'), WHITE, BLACK);
    d[2] = glyph_code(glyph_of('D'), WHITE, BLACK);
    vwrite(NT(21, 0), d, 3);
    d[0] = round >= 10 ? glyph_code(16 + (round / 10) % 10, 7, BLACK) : CH_PANEL;
    d[1] = glyph_code(16 + round % 10, 7, BLACK);
    vwrite(NT(22, 0), d, 2);
}

/* ---- sprites ----------------------------------------------------------------- */

void spr_at(uint8_t n, uint8_t x, uint8_t y, uint8_t frame, uint8_t seat, uint8_t big)
{
    uint16_t px = (uint16_t)x * 3 >> 1;
    son[n] = px < 256;
    sx[n] = (uint8_t)px;
    sy[n] = y < 8 ? 0 : y - 8;
    sfr[n] = frame;
    sseat[n] = seat & 3;
    sbig[n] = big;
}

void spr_arena(uint8_t n, uint8_t x, uint8_t y, uint8_t frame, uint8_t seat)
{
    son[n] = 1;
    sx[n] = 40 + x;
    sy[n] = 8 + y;
    sfr[n] = frame;
    sseat[n] = seat & 3;
    sbig[n] = 0;
}

void spr_hide(uint8_t n)
{
    son[n] = 0;
}

void spr_hide_all(void)
{
    son[0] = son[1] = son[2] = son[3] = 0;
}

static void put_sprite(uint8_t slot, uint8_t x, uint8_t y, uint8_t name, uint8_t colour)
{
    uint8_t *p = sat + slot * 4;
    p[0] = y - 1;
    p[1] = x;
    p[2] = name;
    p[3] = colour;
}

/* The animated tiles' characters, redrawn for the phase: what this frame has time for. */
static uint8_t anim_wanted(uint8_t k)
{
    if (anim_use[k])
        return 1;
    if (k >= AK_FIRE)
        return stat_on[k - AK_FIRE];
    return (k == AK_BOMB || k == AK_DUD) && (uint16_t)(frame_count - bombs_t) < 30;
}

/* kind k's 4 characters (consecutive codes) at phase ph: in each third, the
 * 4 patterns as one write and the 4 colours as another */
static void put_anim(uint8_t k, uint8_t ph)
{
    const uint8_t *b = anim4[k][ph];
    uint16_t a = (uint16_t)(anim_base + k * 4) * 8;
    uint8_t was = blanked;
    blanked = gfx_fast;
    if (f18a) {                 /* each plane's 4 patterns, then the 4 attributes */
        b = f_anim[k][ph];
        vwrite(VR_PAT + a, b, 32);
        vwrite(VR_PAT + a + FV_PLANE, b + 32, 32);
        vwrite(VR_PAT + a + 2 * FV_PLANE, b + 64, 32);
        vwrite(FV_ATTR + (a >> 3), b + 96, 4);
        blanked = was;
        return;
    }
    vwrite(VR_PAT + a, b, 32);
    vwrite(VR_PAT + a + 0x800, b, 32);
    vwrite(VR_PAT + a + 0x1000, b, 32);
    vwrite(VR_COL + a, b + 32, 32);
    vwrite(VR_COL + a + 0x800, b + 32, 32);
    vwrite(VR_COL + a + 0x1000, b + 32, 32);
    blanked = was;
}

static uint8_t done_ph = 255;           /* every kind wanted is drawn at this phase */

static void anim_restart(void)
{
    done_ph = 255;
}

static void anim_step(void)
{
    uint8_t ph = (frame_count >> 3) & 3, k, tries, budget = gfx_fast ? 2 : 1;
    if (ph == done_ph)
        return;                 /* (a kind that turns up meanwhile waits for the next phase) */
    for (tries = 0; tries < N_ANIM && budget; ++tries) {
        k = anim_next;
        if (++anim_next >= N_ANIM)
            anim_next = 0;
        if (anim_ph[k] == ph || !anim_wanted(k))
            continue;
        put_anim(k, ph);
        anim_ph[k] = ph;
        --budget;
    }
    if (tries == N_ANIM)
        done_ph = ph;
}

/* Draw the sprites as set (frame_wait does): the attribute table, and the
 * animation. */
/* 16 sprites off the bottom */
static const uint8_t sat_off[64] = {
    0xC0, 0, 0, 0, 0xC0, 0, 0, 0, 0xC0, 0, 0, 0, 0xC0, 0, 0, 0,
    0xC0, 0, 0, 0, 0xC0, 0, 0, 0, 0xC0, 0, 0, 0, 0xC0, 0, 0, 0,
    0xC0, 0, 0, 0, 0xC0, 0, 0, 0, 0xC0, 0, 0, 0, 0xC0, 0, 0, 0,
    0xC0, 0, 0, 0, 0xC0, 0, 0, 0, 0xC0, 0, 0, 0, 0xC0, 0, 0, 0,
};

void spr_flush(void)
{
    static uint8_t rot;
    uint8_t n, m, near, crowd = 0, base, f, q, was = blanked, y, c, *p;
    memcpy(sat, sat_off, sizeof(sat));
    for (n = 0; n < SEATS && !crowd; ++n) {
        if (!son[n] || sbig[n])
            continue;
        near = 0;
        y = sy[n];
        for (m = 0; m < SEATS; ++m)
            if (son[m] && !sbig[m] && (uint8_t)(sy[m] - y) < 16)
                ++near;
        if (near >= 3)
            crowd = 1;
    }
    rot = crowd ? (rot + 1) & 3 : 0;
    for (n = 0; n < SEATS; ++n) {
        if (!son[n])
            continue;
        if (sbig[n]) {
            f = sfr[n] - FR_WIN0 > 1 ? 0 : sfr[n] - FR_WIN0;
            for (q = 0; q < 4; ++q) {
                if (f18a) {     /* one 8-colour sprite a quarter */
                    put_sprite(8 + q, sx[n] + (q & 1) * 16, sy[n] + (q >> 1) * 16,
                               (N_FRAMES + f * 4 + q) * 4, sseat[n] << 1);
                    continue;
                }
                base = (N_FRAMES * 2 + f * 8 + q) * 4;
                put_sprite(8 + q, sx[n] + (q & 1) * 16, sy[n] + (q >> 1) * 16, base, BLACK);
                put_sprite(12 + q, sx[n] + (q & 1) * 16, sy[n] + (q >> 1) * 16, base + 16, seat_colour[sseat[n]]);
            }
        } else if (f18a) {      /* one sprite, the seat's group */
            p = sat + (n << 2);
            p[0] = sy[n] - 1;
            p[1] = sx[n];
            p[2] = sfr[n] << 2;
            p[3] = sseat[n] << 1;
        } else {
            p = sat + (((n + rot) & 3) << 3);   /* sprites 2k and 2k+1 */
            y = sy[n] - 1;
            c = sfr[n] << 3;
            p[0] = y;
            p[1] = sx[n];
            p[2] = c;
            p[3] = BLACK;
            p[4] = y;
            p[5] = sx[n];
            p[6] = c + 4;
            p[7] = seat_colour[sseat[n]];
        }
    }
    blanked = was || gfx_fast;
    vwrite(VR_SAT, sat, sizeof(sat));
    blanked = was;
    if (arena_on)
        anim_step();
}

/* SYM: the other way (F18A or stock) at once, the screen as it was. */
void gfx_toggle(void)
{
    uint8_t i, r, c, on = !blanked, line[32];
    if (!f18a_found)
        return;
    f18a ^= 1;
    vdp_mode();                         /* (display off, blanked) */
    if (!f18a) {
        vreg(49, 0);
        f_palette(tms_pal, 16);
    } else {
        f_palette(scr_bg == BLACK ? f_pal_arena : f_pal_menu, 64);
    }
    put_blank(scr_bg);
    if (arena_on) {
        arena_cells_load();
        memset(anim_ph, 255, sizeof(anim_ph));
        anim_restart();
    }
    for (i = 0; i < fc_n; ++i)
        put_glyph(fc_code[i], fc_key[i]);
    if (logo_code) {
        if (f18a)
            for (i = 0; i < F_LOGO_CELLS; ++i)
                put_fcell(logo_code + i, f_logo_cells[i], 1);
        else
            for (i = 0; i < LOGO_CELLS; ++i)
                put_cell(logo_code + i, logo_cells[i], 1);
        for (r = 0; r < 3; ++r)         /* (the two logos' widths differ) */
            vfill(NT(tms_row(logo_row + r), 0), CH_PANEL, 32);
        gfx_logo(logo_row);
    }
    if (trophy_code)
        for (i = 0; i < 4; ++i) {
            if (f18a)
                put_fcell(trophy_code + i, f_trophy_cells[i], 1);
            else
                put_cell(trophy_code + i, trophy_cells[i], 1);
        }
    if (arena_on) {                     /* the tiles' characters differ: drawn again */
        for (r = 1; r < 23; ++r) {
            memcpy(line, nt + r * 32, 32);
            floor_line(line, r);
            memcpy(nt + r * 32, line, 32);
        }
        for (r = 0; r < ARENA_H; ++r)
            for (c = 0; c < ARENA_W; ++c) {
                i = at_tile[r * ARENA_W + c];
                if (i != 255) {
                    const uint8_t *t = tchars[i];
                    uint8_t *p = nt + (1 + r * 2) * 32 + ARENA_COL + c * 2;
                    p[0] = t[0];
                    p[1] = t[1];
                    p[32] = t[2];
                    p[33] = t[3];
                }
            }
    }
    vwrite(VR_NAME, nt, 768);           /* (from itself: the copy is a no-op) */
    if (arena_on) {
        for (i = 0; i < 5; ++i)
            if (stat_set & 1 << i)
                gfx_stat(i, stat_n[i]);
        if (round_set)
            gfx_round(round_n);
    }
    if (on) {
        frame_wait();
        vreg(1, R1_ON);
        blanked = 0;
    }
}

#endif
