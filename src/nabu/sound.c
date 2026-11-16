/*
 * sound.c -- the effects (voice 1: channel B, tone or noise) and jingles
 * (voice 0: channel A) on the NABU's AY-3-8910 (41h latch, 40h data),
 * stepped a frame at a time by the frame interrupt (plat.c vdp_frame ->
 * snd_frame), so a note ends on time however long the game is busy. From
 * the Adam's src/adam/sound.c: the same tunes (src/tms9918/tunes.h).
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
 *
 * PLAT_MUSIC: the round's music (music/play.txt, tools/mkmus_ay.py ->
 * gen/nabu/music.c) on the channel nothing else uses, C (ch1: the bass and
 * kick as tones, snare and hat on the noise while no effect holds it), and
 * on A (ch2, the chatter), which a jingle takes: a jingle ends the loop.
 * M cycles all, effects only, none.
 */
#ifdef __NABU__

#include "sound.h"
#include "nabu.h"

__sfr __at 0x41 PSG_ADDR;
__sfr __at 0x40 PSG_DATA;

#include "tms9918/tunes.h"     /* the Adam's and the MSX's: the same divisors */

#define MIX_OFF 0x7F            /* every tone and noise off; port A out, B in */

static const Note *vp[2];       /* the tune going on each voice */
static uint8_t vf[2];           /* frames left of its note */
static uint8_t vvol[2];         /* each voice's volume now (to put back after a hush) */
static uint8_t mix = MIX_OFF;
static uint8_t muted;           /* 1 nothing; PLAT_MUSIC: 2 no loop, 3 nothing */
static uint8_t fxnoise;         /* an effect has the noise: the drums keep out */
#ifdef PLAT_MUSIC
static uint8_t cvol;            /* channel C's volume (the loop's) */
#endif
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
    if (v)
        fxnoise = 0;
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
        mix = (mix | 0x02 | 0x20) & ~0x10;  /* (and a drum's off C) */
        fxnoise = 1;
    } else {
        psg(v * 2, (uint8_t)n->d);
        psg(v * 2 + 1, (n->d >> 8) & 0x0F);
        mix = v ? (mix | 0x10) & ~0x02 : mix & ~0x01;
        if (v)
            fxnoise = 0;
    }
    vvol[v] = 15 - n->a;
    psg(8 + v, vvol[v]);
    psg(7, mix);
}

#ifdef PLAT_MUSIC
extern const uint8_t *const m_sub1[], *const m_sub2[];
extern const uint8_t m_ord1[], m_ord2[], m_loop;

static const uint8_t *mp[2];    /* each channel's next step */
static uint8_t mo[2];           /* its place in the order */
static uint8_t ml[2];           /* jiffies left of its step */
static uint8_t mon;             /* the loop is playing */

/* A jiffy of the loop: ch1 on C, ch2 on A (steps: lo, hi, volume, jiffies). */
static void mus_step(void)
{
    uint8_t s, n;
    const uint8_t *p;
    for (s = 0; s < 2; ++s) {
        if (ml[s] > 1) {
            --ml[s];
            continue;
        }
        p = mp[s];
        if (!p[3]) {            /* the pattern's end: the next in the order */
            const uint8_t *o = s ? m_ord2 : m_ord1;
            n = o[++mo[s]];
            if (n == 255)
                n = o[mo[s] = m_loop];
            p = (s ? m_sub2 : m_sub1)[n];
        }
        ml[s] = p[3];
        mp[s] = p + 4;
        if (s) {                /* the chatter, on A */
            if ((vvol[0] = p[2]) != 0) {
                psg(0, p[0]);
                psg(1, p[1]);
                mix &= ~0x01;
            } else {
                mix |= 0x01;
            }
            psg(8, vvol[0]);
        } else if (p[1] & 0x80) {       /* a drum: C's noise */
            mix |= 0x04;
            if (fxnoise) {
                mix |= 0x20;
                cvol = 0;
            } else {
                psg(6, p[0]);
                mix &= ~0x20;
                cvol = p[2];
            }
            psg(10, cvol);
        } else {                /* the bass, the kick: C's tone */
            mix |= 0x20;
            if ((cvol = p[2]) != 0) {
                psg(4, p[0]);
                psg(5, p[1]);
                mix &= ~0x04;
            } else {
                mix |= 0x04;
            }
            psg(10, cvol);
        }
        psg(7, mix);
    }
}

/* The loop off, C and A quiet (interrupts off). */
static void music_off(void)
{
    mon = 0;
    cvol = vvol[0] = 0;
    psg(10, 0);
    psg(8, 0);
    mix |= 0x25;                /* A's tone, C's tone and noise */
    psg(7, mix);
}
#endif

/* One frame of both voices (from the frame interrupt). */
void snd_frame(void)
{
    uint8_t v;
    const Note *n;
    if (snd_hushed) {
        snd_hushed = 0;
        psg(8, vvol[0]);
        psg(9, vvol[1]);
#ifdef PLAT_MUSIC
        psg(10, cvol);
#endif
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
#ifdef PLAT_MUSIC
    if (mon)
        mus_step();
#endif
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
    if (!(muted & 1))
        start(1, fx[id]);
}

#ifdef PLAT_MUSIC
/* The round's music, both channels from the top on the same frame; or off.
 * Already playing, it plays on. A jingle ends. */
void snd_loop(uint8_t on)
{
    if (on && mon && !vp[0])
        return;
    if (muted)
        on = 0;
    __asm__("di");
    vp[0] = 0;
    music_off();
    if (on) {
        mo[0] = mo[1] = 0;
        mp[0] = m_sub1[m_ord1[0]];
        mp[1] = m_sub2[m_ord2[0]];
        ml[0] = ml[1] = 1;
        mon = 1;
    }
    PSG_ADDR = 14;
    __asm__("ei");
}
#endif

void snd_music(uint8_t id)
{
    if (muted & 1)
        return;
    __asm__("di");
#ifdef PLAT_MUSIC
    music_off();                /* (the jingle has A) */
#endif
    vp[0] = mus[id];
    vf[0] = 1;
    PSG_ADDR = 14;
    __asm__("ei");
}

void snd_stop(void)
{
    __asm__("di");
    vp[0] = vp[1] = 0;
#ifdef PLAT_MUSIC
    music_off();
#endif
    quiet(0);
    quiet(1);
    PSG_ADDR = 14;
    __asm__("ei");
}

/* all, the effects (no music), none */
void snd_toggle_mute(void)
{
#ifdef PLAT_MUSIC
    muted = muted == 0 ? 2 : muted == 2 ? 3 : 0;
    if (muted == 3)
        snd_stop();
    else if (muted)
        snd_loop(0);
#else
    muted ^= 1;
    snd_stop();
#endif
}

#endif
