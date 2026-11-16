/*
 * input.c -- the NABU's keyboard and joysticks (from M.U.L.E.'s NABU
 * input.c), from the queue of codes nabu_hw.asm's keyboard handler keeps
 * (also those that came while an HCCA transaction had interrupts off).
 *
 * Keys: the arrows walk, SPACE or the joystick's button is fire, RETURN,
 * ESC (leaves the table) and BS (DEL) as they say; M turns the sound off and
 * on (main.c). Either joystick steers.
 *
 * Joystick messages are a prefix (80h joystick 1, 81h joystick 2) then a
 * byte A0h-BFh (left 1, down 2, right 4, up 8, button 10h). An arrow sends
 * E0h-E3h (right, left, up, down) going down and 10h more coming up: the
 * direction is held between the two. SPACE has no key-up, so a press holds
 * the button for KEY_HOLD frames. A Master System pad's button 2 is heard
 * only when it is let go (84h on paddle 1, then 4 status bytes C0h-DFh): a
 * fire press then (the glove's throw).
 */
#ifdef __NABU__

#include "input.h"
#include "nabu.h"

uint8_t inp_dir, inp_btn, ev_btn, ev_key;
uint8_t inp_hook, inp_hook_dir, inp_hook_btn, inp_hook_key;

#define JOY_LEFT  0x01
#define JOY_DOWN  0x02
#define JOY_RIGHT 0x04
#define JOY_UP    0x08
#define JOY_FIRE  0x10
#define ARROW_HELD 255
#define KEY_HOLD  8             /* frames a SPACE counts as held */

static uint8_t btn_prev, btn_latch, key_latch, hold, hold_dir, space_hold, tapped;
static uint8_t joy[2], jsel;

/* E0h-E3h: right, left, up, down */
static const uint8_t arrow_dir[4] = { IN_RIGHT, IN_LEFT, IN_UP, IN_DOWN };

static void key(uint8_t k)
{
    uint8_t d;
    if (k == 0xE8) {                    /* SYM: the other video (F8h: let go) */
        gfx_toggle();
        return;
    }
    if ((k & 0xEC) == 0xE0) {           /* an arrow: E0h-E3h down, F0h-F3h up */
        d = arrow_dir[k & 3];
        if (!(k & 0x10)) {
            hold_dir = d;
            hold = ARROW_HELD;
            tapped = 1;
        } else if (hold_dir == d) {
            hold = tapped;              /* down and up in one frame (a tap): still this frame */
        }
        return;
    }
    if (k == ' ') {
        space_hold = KEY_HOLD;
        btn_latch = 1;
        return;
    }
    if (k >= 'a' && k <= 'z')
        k -= 32;
    if (k == 0x7F)
        k = KEY_BACK;
    if (key_latch == KEY_NONE && (k == 13 || k == 27 || k == 8 || (k > 32 && k < 96)))
        key_latch = k;
}

void input_init(void)
{
}

void input_poll(void)
{
    uint8_t dir = 0, btn = 0, k, j;

    tapped = 0;
    while ((k = kb_get()) != 0) {
        if (k >= 0x80 && k <= 0x83)         /* joystick 1-4: its byte follows */
            jsel = k & 3;
        else if ((k & 0xE0) == 0xA0) {      /* a joystick byte */
            if (jsel < 2)
                joy[jsel] = k & 0x1F;
        } else if (k == 0x84)               /* an SMS pad's button 2, let go */
            btn_latch = 1;
        else if (k < 0x80 || k >= 0xE0)     /* (85h-9Fh, C0h-DFh: paddles, status) */
            key(k);
    }
    if (hold) {
        dir = hold_dir;
        if (hold != ARROW_HELD)
            --hold;
    }
    if (space_hold) {
        btn = 1;
        --space_hold;
    }
    j = joy[0] | joy[1];
    if (j & JOY_UP)    dir |= IN_UP;
    if (j & JOY_DOWN)  dir |= IN_DOWN;
    if (j & JOY_LEFT)  dir |= IN_LEFT;
    if (j & JOY_RIGHT) dir |= IN_RIGHT;
    if (j & JOY_FIRE)  btn = 1;

    if (inp_hook) {
        dir = inp_hook_dir;
        btn = inp_hook_btn;
        if (inp_hook_key) {
            key_latch = inp_hook_key;
            inp_hook_key = 0;
        }
    }
    inp_dir = dir;
    inp_btn = btn;
    if (btn && !btn_prev)
        btn_latch = 1;
    btn_prev = btn;
}

void input_take(void)
{
    ev_btn = btn_latch;
    btn_latch = 0;
    ev_key = key_latch;
    key_latch = KEY_NONE;
}

#endif
