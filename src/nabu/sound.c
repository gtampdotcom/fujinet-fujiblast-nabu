/*
 * sound.c -- the effects (voice 1: channel B, tone or noise) and jingles
 * (voice 0: channel A) on the NABU's AY-3-8910 (41h latch, 40h data),
 * stepped a frame at a time by the frame interrupt (plat.c vdp_frame ->
 * snd_frame), so a note ends on time however long the game is busy. From
 * the Adam's src/adam/sound.c: the same tunes.
 *
 * A note is a divisor (0 a rest; bit 15 the noise, its SN76489 control in
 * the low bits), frames and an attenuation (0 loud, 15 off). The AY at the
 * NABU's 1.79 MHz divides by 16 what the Adam's SN76489 at 3.58 MHz divides
 * by 32: the same divisor is the same note. Its volume is 15 less the
 * attenuation. A frame count of 0 ends the tune.
 *
 * Register 7 keeps port A an output (the interrupt mask) and B an input;
 * the latch is left at register 14, as the rest of the port expects. An
 * HCCA reply that keeps us waiting has nabu_hw.asm set the volumes to 0
 * (snd_hushed): the next frame puts them back.
 */
#ifdef __NABU__

#include "sound.h"
#include "nabu.h"

__sfr __at 0x41 PSG_ADDR;
__sfr __at 0x40 PSG_DATA;

typedef struct {
    uint16_t d;
    uint8_t f, a;
} Note;

#define HZ(f)  ((uint16_t)(111861UL / (f)))
#define NOISE(c) (0x8000 | (c))
#define END    { 0, 0, 0 }

#define G4 HZ(392)
#define A4 HZ(440)
#define B4 HZ(494)
#define C5 HZ(523)
#define D5 HZ(587)
#define E5 HZ(659)
#define G5 HZ(784)
#define A5 HZ(880)
#define C6 HZ(1047)

#define NT(n, l) { n, (l) - 3, 2 }, { n, 3, 8 }     /* a note, and its fall */
#define RS(l)    { 0, l, 15 }

static const Note s_drop[]   = { { 300, 2, 2 }, { 220, 2, 4 }, { 160, 3, 6 }, END };
static const Note s_boom[]   = { { NOISE(6), 3, 0 }, { NOISE(6), 3, 2 }, { NOISE(6), 4, 4 }, { NOISE(6), 4, 6 },
                                 { NOISE(6), 5, 8 }, { NOISE(6), 6, 10 }, { NOISE(6), 6, 12 }, { NOISE(6), 8, 14 }, END };
static const Note s_pickup[] = { { G4, 3, 2 }, { B4, 3, 2 }, { D5, 3, 2 }, { G5, 5, 2 }, END };
static const Note s_kick[]   = { { 600, 2, 2 }, { 420, 2, 4 }, END };
static const Note s_throw[]  = { { 140, 2, 4 }, { 180, 2, 3 }, { 240, 2, 3 }, { 360, 3, 4 }, END };
static const Note s_fizzle[] = { { NOISE(5), 6, 4 }, { NOISE(5), 8, 7 }, { NOISE(5), 10, 10 }, { NOISE(5), 10, 13 }, END };
static const Note s_die[]    = { { 190, 4, 2 }, { 230, 4, 2 }, { 270, 4, 3 }, { 330, 5, 4 }, { 400, 6, 4 },
                                 { 470, 6, 5 }, { 560, 8, 6 }, { 700, 10, 8 }, END };
static const Note s_beep[]   = { { A4, 6, 3 }, END };
static const Note s_go[]     = { { A5, 16, 2 }, END };
static const Note s_select[] = { { C5, 2, 4 }, END };

static const Note m_title[] = { NT(G4, 8), NT(C5, 8), NT(E5, 8), NT(G5, 16), NT(E5, 8), NT(G5, 24),
                                RS(8), NT(A5, 8), NT(G5, 8), NT(E5, 8), NT(C6, 32), END };
static const Note m_round[] = { NT(C5, 6), NT(C5, 6), NT(G5, 18), END };
static const Note m_win[]   = { NT(E5, 8), NT(G5, 8), NT(C6, 24), RS(4), NT(G5, 8), NT(C6, 32), END };
static const Note m_game[]  = { NT(C5, 8), NT(E5, 8), NT(G5, 8), NT(C6, 16), NT(A5, 8), NT(C6, 16),
                                NT(G5, 8), NT(E5, 8), NT(G5, 8), NT(C6, 40), END };

static const Note *const fx[SND_COUNT] = {
    s_drop, s_boom, s_pickup, s_kick, s_throw, s_fizzle, s_die, s_beep, s_go, s_select,
};
static const Note *const mus[MUS_COUNT] = { m_title, m_round, m_win, m_game };

#define MIX_OFF 0x7F            /* every tone and noise off; port A out, B in */

static const Note *vp[2];       /* the tune going on each voice */
static uint8_t vf[2];           /* frames left of its note */
static uint8_t vvol[2];         /* each voice's volume now (to put back after a hush) */
static uint8_t mix = MIX_OFF;
static uint8_t muted;
volatile uint8_t snd_hushed;    /* nabu_hw.asm: the volumes went to 0 (a long HCCA wait) */

/* A register (interrupts off: from the frame interrupt, or with DI around). */
static void psg(uint8_t r, uint8_t v)
{
    PSG_ADDR = r;
    PSG_DATA = v;
}

static void quiet(uint8_t v)
{
    vvol[v] = 0;
    psg(8 + v, 0);
    mix |= v ? 0x12 : 0x01;     /* B's tone and noise, or A's tone, off */
    psg(7, mix);
}

static void put(uint8_t v, const Note *n)
{
    uint8_t c;
    if (!n->d) {
        quiet(v);
        return;
    }
    if (n->d & 0x8000) {        /* the noise, on channel B */
        c = n->d & 3;
        psg(6, c == 2 ? 31 : c == 1 ? 20 : 12);
        mix = (mix | 0x02) & ~0x10;
    } else {
        psg(v * 2, (uint8_t)n->d);
        psg(v * 2 + 1, (n->d >> 8) & 0x0F);
        mix = v ? (mix | 0x10) & ~0x02 : mix & ~0x01;
    }
    vvol[v] = 15 - n->a;
    psg(8 + v, vvol[v]);
    psg(7, mix);
}

/* One frame of both voices (from the frame interrupt). */
void snd_frame(void)
{
    uint8_t v;
    const Note *n;
    if (snd_hushed) {
        snd_hushed = 0;
        psg(8, vvol[0]);
        psg(9, vvol[1]);
    }
    for (v = 0; v < 2; ++v) {
        if (!vp[v])
            continue;
        if (vf[v] > 1) {
            --vf[v];
            continue;
        }
        n = vp[v]++;
        if (!n->f) {
            vp[v] = 0;
            quiet(v);
            continue;
        }
        put(v, n);
        vf[v] = n->f;
    }
    PSG_ADDR = 14;
}

static void start(uint8_t v, const Note *t)
{
    __asm__("di");              /* (the interrupt would step it half set) */
    vp[v] = t;
    vf[v] = 1;
    __asm__("ei");
}

void snd_init(void)
{
    __asm__("di");
    mix = MIX_OFF;
    psg(7, mix);
    psg(8, 0);
    psg(9, 0);
    psg(10, 0);
    PSG_ADDR = 14;
    __asm__("ei");
}

void snd_play(uint8_t id)
{
    if (!muted)
        start(1, fx[id]);
}

void snd_music(uint8_t id)
{
    if (!muted)
        start(0, mus[id]);
}

void snd_stop(void)
{
    __asm__("di");
    vp[0] = vp[1] = 0;
    quiet(0);
    quiet(1);
    PSG_ADDR = 14;
    __asm__("ei");
}

void snd_toggle_mute(void)
{
    muted ^= 1;
    snd_stop();
}

#endif
