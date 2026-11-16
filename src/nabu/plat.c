/*
 * plat.c -- the NABU PC: start-up, the 60 Hz clock and quitting (from the
 * Adam's src/adam/plat.c and M.U.L.E.'s NABU plat.c).
 *
 * The clock is the VDP's frame interrupt: our handler (nabu_hw.asm's
 * vdp_isr, in the IM 2 table in place of z88dk's) calls vdp_frame below,
 * which reads the status, counts the frame and steps the sound. While an
 * HCCA transaction has interrupts off (a millisecond or two) a frame can be
 * late; frame_wait's catching up keeps the game in real time. If the
 * interrupt never comes, frame_wait makes frames up from a spin count.
 */
#ifdef __NABU__

#include "plat.h"
#include "gfx.h"
#include "input.h"
#include "sound.h"
#include "nabu.h"
#include "hcca.h"
#include "dbg.h"

uint16_t frame_count;
uint8_t vdp_seen, gfx_fast;
uint8_t plat_will_draw = 1;     /* (play.c: PLAT_SEATS_WHEN_DRAWN) this pass draws */
uint16_t nabu_passes, nabu_late, nabu_skip;     /* for a profile */
static uint16_t seed = 0x1234;
static uint8_t synth;           /* frames made up in a row (0: the interrupt works) */

#define FC (*(volatile uint16_t *)&frame_count)

/* The VDP's frame interrupt (vdp_isr: registers saved, interrupts off). */
void vdp_frame(void)
{
    uint8_t s = vdp_status();   /* (reading it lets the interrupt go) */
    vdp_seen |= s;
    if (s & 0x80) {
        ++frame_count;
        snd_frame();
    }
}

void plat_init(void)
{
    snd_hush();                 /* whatever the boot ROM left sounding */
    hcca_init();
    kb_install();
    (void)ay_vdp_int_on();      /* int_mask: before the frame's handler can use it */
    vdp_install();
    snd_init();
    input_init();
    gfx_init();
    dbg("start");
}

/* Back to the boot ROM (the adapter's menu). */
void plat_quit(void)
{
    snd_stop();
    __asm
        di
        ld      a,2             ; the boot ROM back in
        out     (0),a
        rst     0
    __endasm;
}

/* One pass of the game is a jiffy of logic, so a pass that comes after the
 * next frame has already come is made up by one that doesn't wait or draw:
 * the game keeps real time when the drawing is slower than 60 a second.
 * Called as the frame comes, the blank is ours to draw in flat out. */
#ifndef CATCH_UP
#define CATCH_UP 20             /* frames behind we make up; further, we let go */
#endif
#define SPINS_WAIT  6000u       /* ~60 T-states a spin: ~6 frames, then make one up */
#define SPINS_SYNTH 1000u       /* ... about a frame, once the interrupt has plainly failed */

void frame_wait(void)
{
    static uint16_t seen;
    uint16_t d = FC - seen, spins = 0, limit;
    ++nabu_passes;
    if (d > CATCH_UP) {
        seen = FC - 1;
        d = 1;
    }
    if (d >= 2) {
        ++seen;                 /* behind: a pass with no waiting, no drawing */
        ++nabu_skip;
        plat_will_draw = 0;
        return;
    }
    plat_will_draw = 1;
    gfx_fast = d == 0;
    nabu_late += d;
    limit = synth >= 3 ? SPINS_SYNTH : SPINS_WAIT;
    while (FC == seen) {
        if (++spins >= limit) { /* no interrupt: make the frame up */
            __asm__("di");
            ++frame_count;
            __asm__("ei");
            if (synth < 255)
                ++synth;
            gfx_fast = 0;
            break;
        }
    }
    if (spins < limit)
        synth = 0;
    seen = FC;
    seed += seen;
    spr_flush();
}

uint8_t plat_rand(void)
{
    /* xorshift, stirred by the frame count */
    seed ^= seed << 7;
    seed ^= seed >> 9;
    seed ^= seed << 8;
    return (uint8_t)seed;
}

#endif
