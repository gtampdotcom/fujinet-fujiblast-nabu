/*
 * nabu.h -- the NABU PC: the TMS9918A, the frame interrupt, and the few
 * routines written in assembler (vdp.asm, nabu_hw.asm). From the Adam's
 * adam.h: the same VDP, at ports A0h (data) and A1h (control).
 *
 * The VDP's interrupt reaches the Z80 through the AY's interrupt mask (IM 2,
 * nabu_hw.asm's vdp_isr). It reads the VDP's status, which also resets the
 * address latch, so it must not land between the two bytes of an address
 * or inside a burst of data: while the game is at the VDP (vdp_busy) the
 * handler takes the VDP out of the mask and says so in vdp_pend, and the
 * unlock (vdp_catch_up) puts it back, when the frame comes at once.
 *
 * In Graphics II the CPU may touch the VDP only every 8 us while the
 * screen is drawn (29 T-states at the Adam's clock, 29 at the NABU's
 * 3.58 MHz): the writers here are slower than that, except va_writef,
 * which is for the frame's blank and a blanked display.
 */
#ifndef NABU_H
#define NABU_H

#include <stdint.h>

__sfr __at 0xA0 VDP_DATA;
__sfr __at 0xA1 VDP_CTRL;

extern volatile uint8_t vdp_busy, vdp_pend;
extern uint8_t vdp_seen;        /* status bits seen by the frame clock */
void vdp_catch_up(void);
#define VDP_LOCK()   (vdp_busy = 1)
#define VDP_UNLOCK() do { vdp_busy = 0; if (vdp_pend) vdp_catch_up(); } while (0)

/* VRAM (Graphics II: three thirds, each with its own 256 patterns and colours) */
#define VR_PAT   0x0000         /* thirds at +0, +800h, +1000h; 8 bytes a character */
#define VR_NAME  0x1800         /* 32 x 24 */
#define VR_SAT   0x1B00
#define VR_COL   0x2000         /* laid out as the patterns */
#define VR_SPAT  0x3800         /* sprite patterns: 16 x 16, 32 bytes each */

/* vdp.asm: the arguments are these variables */
extern uint16_t va_addr, va_len;
extern const uint8_t *va_src;
extern uint8_t va_val;
void va_write(void);            /* va_len bytes from va_src to VRAM va_addr */
void va_writef(void);           /* the same, flat out: blank or blanked display only */
void va_fill(void);             /* va_len bytes of va_val */

/* nabu_hw.asm */
void kb_install(void);          /* our keyboard handler in the IM 2 table */
void vdp_install(void);         /* our frame handler */
uint8_t kb_get(void);           /* the next keyboard code, 0 if none */
uint8_t vdp_status(void);

/* gfx.c: set by frame_wait: this frame is just past the VDP interrupt, so
 * the blank is ours to write in flat out */
extern uint8_t gfx_fast;

/* sound.c: a frame of the sound (from the frame interrupt) */
void snd_frame(void);

#endif
