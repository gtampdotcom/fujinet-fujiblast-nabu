/*
 * menus.c -- the screens before a table: the title, the name, the rooms.
 * (The Apple II keeps them in an overlay: src/apple2/overlay.c.)
 */
#ifdef NO_MENUS                         /* a test build takes no menus (the 7800's bank 0 is small) */
#include "game.h"
void title(void)
{
}
void name_entry(void)
{
}
uint8_t table_list(void)
{
    return 0;
}
#else
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "gfxcodes.h"
#include "input.h"
#include "plat.h"
#include "sound.h"

#ifndef TAGLINE
#define TAGLINE "BOMBS, BLOCKS AND BLASTS FOR 2-4"
#endif
#ifndef NAME_HELP
#define NAME_HELP "LETTERS AND DIGITS, THEN " KEY_END_NAME
#endif
#ifndef NAME_PROMPT
#define NAME_PROMPT "WHAT IS YOUR NAME?"
#endif
#ifndef TITLE_Y
#define TITLE_Y(n) 96           /* the title's bombers walk along one line */
#endif

#ifndef KEY_END_NAME
#define KEY_END_NAME "RETURN"   /* (the PC's key says ENTER) */
#endif

#ifdef __NABU__
/* with an F18A (or a PICO9918), SYM switches its colour screens and the
 * stock TMS9918A ones at any time (src/nabu/gfx.c, input.c): the title says so */
extern uint8_t f18a, f18a_found;
void gfx_text_half(uint8_t row, uint8_t col, const char *s, uint8_t colour);
static void video_hint(void)
{
    centre(23, f18a ? "SYM: VIDEO F18A   " QUIT_HINT : "SYM: VIDEO STOCK  " QUIT_HINT, 0, 40);
}
#endif

void title(void)
{
    uint8_t n, x;
    uint16_t t = 0;
#ifdef __NABU__
    uint8_t was_f18a;
redraw:                                 /* (SYM: the other video) */
    was_f18a = f18a;
#endif
    gfx_begin();
    gfx_logo(3);
    gfx_row(8, ROW_BIG, PAL_MENU);
#ifdef __NABU__
    gfx_text_half(8, 4, "FOR FUJINET", 0);  /* (11 letters: centred by half a cell) */
#else
    centre(8, "FOR FUJINET", 0, 20);
#endif
    centre(18, "PRESS FIRE", 0, 40);
    centre(21, TAGLINE, 0, 40);
#ifdef QUIT_HINT
#ifdef __NABU__
    if (f18a_found)                     /* (the switch beside it) */
        video_hint();
    else
#endif
    centre(23, QUIT_HINT, 0, 40);       /* (a platform that can quit) */
#endif
    gfx_show();
    if (!t)
        snd_music(MUS_TITLE);
    for (;;) {
        tick();
        ++t;
        for (n = 0; n < SEATS; ++n) {
            x = (uint8_t)((t >> 1) + n * 40) % 168;
            spr_at(n, x, TITLE_Y(n), FR_RIGHT0 + ((t >> 3) & 1 ? (n & 1) + 1 : 0), n, 0);
        }
        if (ev_btn || ev_key == KEY_RETURN)
            break;
#ifdef QUIT_HINT
        if (ev_key == KEY_ESC)
            plat_quit();
#endif
#ifdef __NABU__
        if (f18a != was_f18a)
            goto redraw;
#endif
    }
    snd_stop();
    spr_hide_all();
}

#ifdef PLAT_NAME_WHEEL
/* No keyboard: the stick up and down turns a wheel (letters, digits, then
 * the end), fire or right takes the one at the cursor, left backs up. */
void name_entry(void)
{
    static const char wheel[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    uint8_t n = 0, w = 0;
    char b[2];
    gfx_begin();
    gfx_row(6, ROW_BIG, PAL_MENU);
    centre(6, NAME_PROMPT, 0, 20);
    gfx_row(10, ROW_BIG, PAL_MENU);
    centre(14, NAME_HELP, 0, 40);
    gfx_show();
    my_name[0] = 0;
    b[1] = 0;
    for (;;) {
        gfx_clear_row(10);
        gfx_text(10, 6, my_name, 1);
        if (n < NAME_LEN) {
            b[0] = w == 36 ? '>' : wheel[w];
            gfx_text(10, 6 + n, b, 0);
        }
        prev_dir = inp_dir;
        tick();
        if (pressed(IN_UP))
            w = w ? w - 1 : 36;
        if (pressed(IN_DOWN))
            w = w < 36 ? w + 1 : 0;
        if (pressed(IN_LEFT) && n)
            my_name[--n] = 0;
        if (ev_btn || pressed(IN_RIGHT)) {
            if (w == 36) {
                if (n >= 2)
                    break;
            } else if (n < NAME_LEN) {
                my_name[n++] = wheel[w];
                my_name[n] = 0;
                snd_play(SND_SELECT);
            }
        }
    }
}
#else
void name_entry(void)
{
    uint8_t n = 0, redraw = 1;
    char c;
#ifdef __NABU__
again:
#endif
    gfx_begin();
    gfx_row(6, ROW_BIG, PAL_MENU);
    centre(6, NAME_PROMPT, 0, 20);
    gfx_row(10, ROW_BIG, PAL_MENU);
    centre(14, NAME_HELP, 0, 40);
#ifdef __NABU__
    centre(16, "ESC: BACK", 0, 40);
#endif
    gfx_show();
#ifdef __NABU__
    /* the Internet Adapter's player name, to keep (RETURN or fire) or rub out;
     * back from the room list (ESC), the name we had */
    {
        extern char name_prefill[];
        if (!my_name[0])
            strcpy(my_name, name_prefill);
        n = strlen(my_name);
    }
#else
    my_name[0] = 0;
#endif
    for (;;) {
        /* Only when it changes, and over what was there (no clearing first):
         * a row cleared and rewritten each frame can be caught blank by the
         * beam, every frame, once the rewrite takes long enough. */
        if (redraw) {
            gfx_text(10, 6, my_name, 1);
            gfx_text(10, 6 + n, n < NAME_LEN ? "- " : " ", 0);
            redraw = 0;
        }
        tick();
        c = ev_key;
        if (((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) && n < NAME_LEN) {
            my_name[n++] = c;
            my_name[n] = 0;
            redraw = 1;
            snd_play(SND_SELECT);
        } else if (c == KEY_BACK && n) {
            my_name[--n] = 0;
            redraw = 1;
        } else if ((c == KEY_RETURN
#ifdef __NABU__
                    || ev_btn
#endif
                   ) && n >= 2) {
            break;
        }
#ifdef __NABU__
        else if (c == KEY_ESC) {        /* back to the title (whose ESC quits) */
            title();
            redraw = 1;
            goto again;
        }
#endif
    }
}
#endif

static void phase_name(char *b, uint8_t ph)
{
    strcpy(b, ph == PH_WAIT ? "WAITING" : "PLAYING");
}

#ifdef __NABU__
/* The NABU's room list: a REFRESH line under the rooms asks the server for
 * the list again (it sends it only when asked), and ESC goes back to the
 * name (returns 2; 1: a room picked, 0: no answer). */
uint8_t table_list(void)
{
    static char b[41];
    uint8_t n, i, j, sel = 0, *t;
    message("CONNECTING", srv_host, 1);
    n = net_tables();
    for (;;) {
        if (!n) {
            net_close();
            message("NO ANSWER FROM", srv_host, 0);
            return 0;
        }
        if (sel > n)
            sel = n;
        gfx_begin();
        gfx_row(1, ROW_BIG, PAL_MENU);
        centre(1, "PICK A ROOM", 0, 20);
        for (i = 0; i < n; ++i) {
            t = tables[i];
            memset(b, ' ', 40);
            b[40] = 0;
            memcpy(b + 4, t + 8, 12);
            for (j = 4; j < 16; ++j)
                if (!b[j])
                    b[j] = ' ';
            b[18] = '0' + t[20];
            b[19] = '/';
            b[20] = '0' + t[21];
            phase_name(b + 24, t[22]);
            gfx_text(5 + i * 2, 0, b, 0);
        }
        gfx_text(5 + n * 2, 4, "REFRESH", 0);
        centre(22, "STICK: CHOOSE    FIRE: PICK", 0, 40);
        centre(24, "ESC: CHANGE NAME", 0, 40);
        gfx_show();
        for (;;) {
            for (i = 0; i <= n; ++i)
                gfx_text(5 + i * 2, 2, i == sel ? ">" : " ", 0);
            prev_dir = inp_dir;
            tick();
            net_poll();
            if (pressed(IN_UP) && sel) {
                --sel;
                snd_play(SND_SELECT);
            }
            if (pressed(IN_DOWN) && sel < n) {
                ++sel;
                snd_play(SND_SELECT);
            }
            if (ev_key == KEY_ESC) {
                net_close();
                return 2;
            }
            if (ev_btn || ev_key == KEY_RETURN)
                break;
        }
        if (sel < n)
            break;
        snd_play(SND_SELECT);
        message("REFRESHING", srv_host, 1);
        n = net_tables();       /* (the link is still open: the list asked again) */
    }
    memcpy(srv_table, tables[sel], TABLE_LEN);
    srv_table[TABLE_LEN] = 0;
    return 1;
}
#else
uint8_t table_list(void)
{
    static char b[41];
    uint8_t n, i, sel = 0, *t;
    message("CONNECTING", srv_host, 1);
    n = net_tables();
    if (!n) {
        net_close();
        message("NO ANSWER FROM", srv_host, 0);
        return 0;
    }
    gfx_begin();
    gfx_row(1, ROW_BIG, PAL_MENU);
    centre(1, "PICK A ROOM", 0, 20);
    for (i = 0; i < n; ++i) {
        t = tables[i];
        memset(b, ' ', 40);
        memcpy(b + 4, t + 8, 12);
        for (sel = 4; sel < 16; ++sel)
            if (!b[sel])
                b[sel] = ' ';
        b[18] = '0' + t[20];
        b[19] = '/';
        b[20] = '0' + t[21];
        phase_name(b + 24, t[22]);
        gfx_text(5 + i * 2, 0, b, 0);
    }
    centre(22, "STICK: CHOOSE    FIRE: JOIN", 0, 40);
    gfx_show();
    sel = 0;
    for (;;) {
        for (i = 0; i < n; ++i)
            gfx_text(5 + i * 2, 2, i == sel ? ">" : " ", 0);
        prev_dir = inp_dir;
        tick();
        net_poll();
        if (pressed(IN_UP) && sel) {
            --sel;
            snd_play(SND_SELECT);
        }
        if (pressed(IN_DOWN) && sel + 1 < n) {
            ++sel;
            snd_play(SND_SELECT);
        }
        if (ev_btn || ev_key == KEY_RETURN)
            break;
    }
    memcpy(srv_table, tables[sel], TABLE_LEN);
    srv_table[TABLE_LEN] = 0;
    return 1;
}
#endif
#endif
