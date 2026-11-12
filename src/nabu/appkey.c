/*
 * appkey.c -- the NABU has no FujiNet, so no lobby appkeys (src/appkey.c on
 * the others): the name is asked for at boot, and the rooms listed are the
 * default server's (gen/config.h, DEFAULT_SERVER "host:port"). The name
 * screen starts with the player name set in the Internet Adapter (an
 * adapter new enough to have one: nstrans.c nst_player_name), letters and
 * digits only, to be kept or rubbed out.
 */
#ifdef __NABU__

#include <string.h>
#include "game.h"
#include "config.h"

char my_name[NAME_LEN + 1];
char srv_host[61];
uint16_t srv_port;
char srv_table[TABLE_LEN + 1];
char name_prefill[NAME_LEN + 1];

void nst_player_name(char *out);     /* nstrans.c */

void appkey_load(void)
{
    const char *p = DEFAULT_SERVER;
    uint8_t n = 0;
    while (*p && *p != ':' && n < 60)
        srv_host[n++] = *p++;
    srv_host[n] = 0;
    srv_port = 0;
    if (*p == ':')
        while (*++p >= '0' && *p <= '9')
            srv_port = srv_port * 10 + (*p - '0');
    if (!srv_port)
        srv_port = STREAM_PORT;
    my_name[0] = 0;
    srv_table[0] = 0;
    {
        static char a[17];
        uint8_t i, c;
        nst_player_name(a);
        n = 0;
        for (i = 0; a[i] && n < NAME_LEN; ++i) {
            c = (uint8_t)a[i];
            if (c >= 'a' && c <= 'z')
                c -= 32;
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
                name_prefill[n++] = c;
        }
        name_prefill[n] = 0;
    }
}

void appkey_save_name(void)
{
}

void appkey_save_room(void)
{
}

void appkey_clear_room(void)
{
    srv_table[0] = 0;
}

#endif
