/*
 * nstrans.c -- the link to the server on the NABU PC: a TCP socket made by
 * the NABU Internet Adapter (RetroNET), spoken to over the HCCA port (from
 * M.U.L.E.'s NABU nstrans.c), carrying FujiBlast's frames (COBS, Fletcher
 * sum: the decoder is the Adam's src/adam/nstrans.c).
 *
 *   D0 open   D0 len host[len] port(16) handle(FF = assign) -> handle (FF: failed)
 *   D1 close  D1 handle
 *   D2 avail  D2 handle -> int32 bytes waiting (-1: disconnected)
 *   D3 read   D3 handle len(16) -> int32 n (-1: disconnected), n bytes
 *   D4 write  D4 handle len(16) data -> int32 written (-1: disconnected)
 *
 * An adapter answers at once (it, not the socket, is the other end of the
 * wire), so each call is one short transaction with interrupts off, a
 * millisecond or two: a write of the frames queued, else a status call (at
 * most one a frame) and a read only when it says bytes wait.
 *
 * A reply that doesn't come (the adapter busy or gone) times out; the wire
 * is then drained so the next call starts clean.
 */
#ifdef __NABU__

#include <string.h>
#include <stdint.h>
#include "nstrans.h"
#include "plat.h"
#include "hcca.h"
#include "dbg.h"

#define RN_TCP_OPEN  0xD0
#define RN_TCP_CLOSE 0xD1
#define RN_TCP_AVAIL 0xD2
#define RN_TCP_READ  0xD3
#define RN_TCP_WRITE 0xD4
#define H_ANY 0xFF

/* Polls of the HCCA status (~97 T-states each) for a reply's first byte
 * (~0.9 s: an emulator relaying the HCCA over a socket can take tens of ms)
 * and for each next one (~85 ms; a byte is 90 us on the wire). */
#define RN_FIRST 34000U
#define RN_NEXT  10500U
#define RN_QUIET 6000U          /* polls of silence that end a drain: ~0.3 s */
#define RN_OPEN_WAIT 12U        /* RN_FIRST waits for an open's reply: ~11 s (DNS, a TCP connect) */

#define DOWN_AFTER 3            /* statuses saying the socket is shut */
#define BAD_AFTER  8            /* calls in a row without an answer */
#define RBUF 240                /* the most a read takes (the server keeps under 192 unacknowledged;
                                 * the reply, 4 + 240, and a write's 4 fit the 256-byte ring) */
#define RN_LATE 90              /* frames a reply may take (1.5 s), then the link has missed one */
#define RN_HUSH 30              /* frames after a reply that never came: late bytes thrown away */
#define FMAX 136                /* a decoded frame, at most */
#define OMAX 64                 /* a frame we send, at most */
#define QMAX 96                 /* frames flushed, not yet written */

uint8_t nst_buf[FMAX];
uint8_t nst_taken;

static uint8_t handle = H_ANY;
static uint8_t down, bad, lost, garbled;
static uint8_t rbuf[RBUF];
static uint8_t rlen, rpos;
static uint16_t polled;         /* the pass that last asked the adapter */
static uint16_t polled_f;       /* and its frame */
#ifndef POLL_GAP
#define POLL_GAP 3              /* frames between two asks: the server sends every 3 */
#endif
extern uint16_t nabu_passes;    /* plat.c: frame_wait's passes */
extern uint8_t plat_will_draw;  /* plat.c: 0 on a pass made up to catch up */
static uint8_t dlen, drem, dzero, dbad;     /* the frame being decoded */
static uint8_t obuf[OMAX], olen, oover;     /* the frame being put */
static uint8_t qbuf[QMAX], qlen;

/* The requests out, their replies not yet in (nabu_hw.asm's interrupt puts
 * the bytes into rxq): a write's (W) and/or a read's (R), in the order sent. */
static uint8_t pk[2], pn;       /* the kinds waited for */
static uint8_t hn, hdr[4];      /* the head reply's int32, as far as it came */
static uint8_t need, got;       /* a read's payload: bytes still to come, bytes in rbuf */
static uint8_t sent_w;          /* the bytes a write sent */
static uint16_t sent_f, hush_f; /* the frame the requests went; till when late bytes go */
static uint8_t hushing;
static uint8_t rxq_r;
static uint8_t tbuf[4 + QMAX + 4];  /* the requests as they go: D4 + frames, D3 */
#define FCV (*(volatile uint16_t *)&frame_count)

/* ---- the wire (hcca_rx_n: a tight loop, no byte lost) ------------------- */

static uint8_t rxn(uint8_t *dst, uint8_t n, uint16_t first)
{
    hcca_pre = 0;
    hcca_dst = dst;
    hcca_cnt = n;
    hcca_tmo1 = first;
    hcca_tmoN = RN_NEXT;
    return hcca_rx_n();
}

/* pre (4) prefix bytes into hcca_hdr, then as many payload bytes as it says */
static uint8_t rxp(uint8_t pre, uint8_t *dst, uint8_t max, uint16_t first)
{
    hcca_pre = pre;
    hcca_dst = dst;
    hcca_cnt = max;
    hcca_tmo1 = first;
    hcca_tmoN = RN_NEXT;
    return hcca_rx_n();
}

static void drain(void)
{
    uint16_t quiet = RN_QUIET, n = 0;
    snd_hush();                 /* (interrupts are off a while: no note may hang) */
    while (quiet--) {
        kb_poll();
        if (hcca_rx_ready()) {
            (void)hcca_rx();
            quiet = RN_QUIET;
            if (++n == 1024)    /* a stuck "ready" must not hang the machine */
                break;
        }
    }
}

static void tx16(uint16_t v)
{
    hcca_tx((uint8_t)v);
    hcca_tx((uint8_t)(v >> 8));
}

/* ---- the link ------------------------------------------------------------- */

uint8_t nst_open(const char *host, uint16_t port)
{
    uint8_t n = 0, i, hb = H_ANY, got = 0, t;

    nst_close();
    while (host[n] && n < 60)
        ++n;
    dbg_s("open", host);
    hcca_begin();
    drain();
    hcca_tx(RN_TCP_OPEN);
    hcca_tx(n);
    for (i = 0; i < n; ++i)
        hcca_tx((uint8_t)host[i]);
    tx16(port);
    hcca_tx(H_ANY);
    for (t = 0; t < RN_OPEN_WAIT && !got; ++t)
        got = rxn(&hb, 1, RN_FIRST);
    if (!got)
        drain();
    hcca_end();
    if (!got || hb == H_ANY) {
        dbg(got ? "open: refused" : "open: no answer");
        return 0;
    }
    handle = hb;
    rlen = rpos = 0;
    dlen = drem = dzero = dbad = 0;
    olen = oover = qlen = 0;
    down = bad = lost = garbled = 0;
    nst_taken = 0;
    polled = nabu_passes;
    polled_f = frame_count - POLL_GAP;
    pn = hn = hushing = 0;
    rxq_r = rxq_w;
    nst_put(0);                 /* ends whatever came before: our first frame is clean */
    return 1;
}

static void collect(void);
static void send_requests(uint8_t read);

/* Till the replies waited for are in (or a second has gone). */
static void settle(void)
{
    uint16_t t = FCV;
    while (pn && (uint16_t)(FCV - t) < 60)
        collect();
    pn = hn = 0;
}

void nst_close(void)
{
    if (handle != H_ANY) {
        settle();
        if (qlen) {
            send_requests(0);   /* (a goodbye goes out) */
            settle();
        }
        hcca_begin();
        hcca_tx(RN_TCP_CLOSE);
        hcca_tx(handle);
        hcca_end();
        dbg("close");
        handle = H_ANY;
    }
    qlen = olen = oover = 0;
    rlen = rpos = 0;
    pn = hn = 0;
}

/* The replies so far, out of the ring: a write's int32 (the bytes written,
 * -1: the socket is gone), a read's int32 (the bytes that follow; -1: gone)
 * and its bytes, into rbuf. */
static void collect(void)
{
    int32_t v;
    while (pn && rxq_r != rxq_w) {
        if (hn < 4) {
            hdr[hn++] = rxq[rxq_r++];
            if (hn < 4)
                continue;
            memcpy(&v, hdr, 4);
            if (pk[0] == 'W') {
                if (v < 0) {
                    if (down < 255)
                        ++down;
                } else if (v != sent_w) {
                    lost = 1;   /* a frame that didn't go whole: the link is out of step */
                }
            } else if (v > 0 && v <= RBUF) {
                need = (uint8_t)v;
                got = 0;
                continue;       /* its bytes follow */
            } else if (v < 0) {
                if (down < 255)
                    ++down;
            } else if (v) {
                garbled = 1;    /* more than asked: can't be */
            } else {
                down = 0;
            }
        } else {
            rbuf[got++] = rxq[rxq_r++];
            if (--need)
                continue;
            rlen = got;
            rpos = 0;
            down = 0;
        }
        bad = 0;                /* a whole reply: the next, if any */
        hn = 0;
        pk[0] = pk[1];
        --pn;
    }
}

/* The frames queued (D4) and, if read, a read (D3), in one go; the replies
 * come by interrupt and collect takes them on later passes: no waiting. */
static void send_requests(uint8_t read)
{
    uint8_t n = 0;
    rxq_r = rxq_w;              /* (nothing is waited for: stray bytes away) */
    if (qlen) {
        tbuf[0] = RN_TCP_WRITE;
        tbuf[1] = handle;
        tbuf[2] = qlen;
        tbuf[3] = 0;
        memcpy(tbuf + 4, qbuf, qlen);
        n = 4 + qlen;
        sent_w = qlen;
        qlen = 0;
        pk[pn++] = 'W';
    }
    if (read) {
        tbuf[n++] = RN_TCP_READ;
        tbuf[n++] = handle;
        tbuf[n++] = RBUF;
        tbuf[n++] = 0;
        pk[pn++] = 'R';
    }
    hn = 0;
    sent_f = FCV;
    hcca_send(tbuf, n);
}

/* One look at the adapter, once a pass at most: what came is taken; with
 * nothing waited for, the frames queued go out at once (our moves: the server
 * judges a blast by where it last heard we were, so every frame they wait
 * counts), and the socket is read every POLL_GAP frames (the server sends
 * every 3), with them when both are due.
 *
 * The replies aren't waited for: a real adapter takes milliseconds to answer
 * (a USB serial link alone ~16 ms), time the game used to spend with
 * interrupts off, polling. They come by interrupt (nabu_hw.asm) while the game
 * runs, and are taken here on a later pass. */
static void step(void)
{
    uint8_t read;
    if (handle == H_ANY)
        return;
    if (hushing) {              /* a reply that never came: wait out late bytes */
        rxq_r = rxq_w;
        if ((uint16_t)(FCV - hush_f) < RN_HUSH)
            return;
        hushing = 0;
    }
    collect();
    if (pn) {
        if ((uint16_t)(FCV - sent_f) >= RN_LATE) {
            if (pk[0] == 'R' || pn > 1)
                garbled = 1;    /* a read's bytes may have been taken and lost */
            if (bad < 255)
                ++bad;
            pn = hn = 0;
            hushing = 1;
            hush_f = FCV;
        }
        return;
    }
    if (polled == nabu_passes)
        return;
    read = rpos >= rlen && (uint16_t)(FCV - polled_f) >= POLL_GAP;
    if (!read && !qlen)
        return;
    polled = nabu_passes;
    if (read)
        polled_f = FCV;
    send_requests(read);
}

/* A frame is gathered, then queued (and written) at its closing 0. */
void nst_put(uint8_t c)
{
    if (olen < OMAX)
        obuf[olen++] = c;
    else
        oover = 1;
    if (c)
        return;
    if (!oover && handle != H_ANY && qlen + olen <= QMAX) {
        memcpy(qbuf + qlen, obuf, olen);
        qlen += olen;           /* it goes with the next requests (step) */
    } else if (handle != H_ANY) {
        lost = 1;
    }
    olen = oover = 0;
}

/* nabu_hw.asm: the Fletcher sum of n bytes (sums mod 255, as add255 made
 * them): s1 in the low byte, s2 the high; ~50 T-states a byte against ~300 */
extern uint16_t nabu_fletcher(const uint8_t *p, uint8_t n);

/* The frame's end: its Fletcher sum checked; its length, or 255. */
static uint8_t frame_end(void)
{
    uint8_t o = dlen;
    uint16_t f;
    if (dbad || drem || o < 3)
        return 255;
    o -= 2;
    f = nabu_fletcher(nst_buf, o);
    if ((uint8_t)f != nst_buf[o] || (uint8_t)(f >> 8) != nst_buf[o + 1])
        return 255;
    return o;
}

/* Bytes up to a frame's end, the COBS undone as they come: a code byte n
 * says n-1 bytes follow, then a 0 (unless n is FFh, or the frame ends). */
uint8_t nst_frame(void)
{
    uint8_t c, n;
    if (rpos >= rlen)
        step();
    while (rpos < rlen) {
        c = rbuf[rpos++];
        ++nst_taken;
        if (!c) {
            n = dlen || drem || dbad ? frame_end() : 0;
            dlen = drem = dzero = dbad = 0;
            if (n)
                return n;
            continue;
        }
        if (dlen >= FMAX - 1) {
            dbad = 1;           /* too long: wait for the next 0 */
            continue;
        }
        if (!drem) {
            if (dzero)
                nst_buf[dlen++] = 0;
            drem = c - 1;
            dzero = c < 0xFF;
        } else {
            nst_buf[dlen++] = c;
            --drem;
        }
    }
    return 0;
}

uint8_t nst_lost(void)
{
    return handle != H_ANY && (lost || down >= DOWN_AFTER || bad >= BAD_AFTER);
}

uint8_t nst_garbled(void)
{
    uint8_t g = garbled;
    garbled = 0;
    return g;
}

/* The player name set in the Internet Adapter's settings (RetroNET
 * Multiplayer, NABU-LIB mp_getPlayerName: EFh 02h -> 16 bytes, NUL ended),
 * into out (17 bytes); "" if none. An adapter older than MP_VERSION never
 * answers EFh, so its version (IA control BAh 16h: length, text) is asked
 * first: that one every adapter since 2024 answers (an older one times out). */
#define MP_VERSION "2026.11.10.00"

void nst_player_name(char *out)
{
    static uint8_t v[17];
    uint8_t len, n, i, stage = 0;
    const char *m = MP_VERSION;

    out[0] = 0;
    memset(v, 0, sizeof(v));
    hcca_begin();
    drain();
    hcca_tx(0xBA);
    hcca_tx(0x16);
    /* length and text in one read (no byte may wait between two calls) */
    n = rxp(1, v, 16, RN_FIRST);
    if (hcca_npre != 1)
        goto fail;
    stage = 1;
    len = hcca_hdr[0];
    if (n != (len > 16 ? 16 : len))
        goto fail;
    if (len > 16)
        drain();                        /* (a longer one than we keep) */
    stage = 2;
    for (i = 0; m[i]; ++i) {            /* yyyy.mm.dd.bb: text order is age */
        if (v[i] > (uint8_t)m[i])
            break;
        if (v[i] < (uint8_t)m[i]) {
            dbg_s("adapter too old for names", (char *)v);
            hcca_end();
            return;
        }
    }
    stage = 3;
    hcca_tx(0xEF);
    hcca_tx(0x02);
    if (rxn((uint8_t *)out, 16, RN_FIRST) != 16) {
        out[0] = 0;
        goto fail;
    }
    out[16] = 0;
    hcca_end();
    dbg_s("adapter name", out);
    return;
fail:
    dbg_n("adapter name: no answer", stage);
    drain();
    hcca_end();
}

/* (no appkeys on the NABU: nothing else uses the wire) */
void nst_pause(void)
{
}

void nst_resume(void)
{
}

#endif
