#include <stddef.h>
#include <string.h>
#include <i86.h>
#include "platform.h"

/* Real serial transport (T14.4) over BIOS INT 14h's classic serial
   services (AH=00h init, 01h send, 02h receive, 03h get status) --
   the same "no networking stack, serial only" scope
   platform/atari/transport_atari.c already established, now for a
   third real target. `endpoint` prefixes other than "serial:"
   (including "tcp:") are WP_UNSUPPORTED here, honestly, not silently
   accepted -- mirrors transport_atari.c's own choice exactly.

   endpoint format: "serial:<device>:<baud>" (platform.h's own doc
   comment establishes this convention). <device> is the BIOS COM port
   number: 0=COM1, 1=COM2, 2=COM3, 3=COM4 (INT 14h's own DX
   convention) -- unlike the ST's single fixed AUX port, real PC
   hardware can have several, so this field is actually meaningful
   here, not just kept for shape as transport_atari.c's own comment
   notes it is on that target. */

static wp_bool parse_port(const char *s, const char *end, int *out_port)
{
    long value = 0;
    wp_bool any_digit = WP_FALSE;

    while (s < end && *s >= '0' && *s <= '9') {
        value = value * 10 + (long)(*s - '0');
        s++;
        any_digit = WP_TRUE;
    }
    if (!any_digit || s != end) return WP_FALSE;
    if (value < 0 || value > 3) return WP_FALSE;
    *out_port = (int)value;
    return WP_TRUE;
}

/* INT 14h AH=00h's own 3-bit baud index -- only these 8 discrete rates
   are selectable at this BIOS layer (unlike the ST's Rsconf, which has
   a richer set transport_atari.c's own parse_baud reflects). */
static wp_bool parse_baud(const char *s, int *out_idx)
{
    long value = 0;
    wp_bool any_digit = WP_FALSE;

    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (long)(*s - '0');
        s++;
        any_digit = WP_TRUE;
    }
    if (!any_digit || *s != '\0') return WP_FALSE;

    switch (value) {
    case 110:  *out_idx = 0; return WP_TRUE;
    case 150:  *out_idx = 1; return WP_TRUE;
    case 300:  *out_idx = 2; return WP_TRUE;
    case 600:  *out_idx = 3; return WP_TRUE;
    case 1200: *out_idx = 4; return WP_TRUE;
    case 2400: *out_idx = 5; return WP_TRUE;
    case 4800: *out_idx = 6; return WP_TRUE;
    case 9600: *out_idx = 7; return WP_TRUE;
    default:   return WP_FALSE;
    }
}

/* AH=03h's own line-status bits (get-port-status and, per the BIOS
   spec, the same byte returned in AH by the send/receive calls). */
#define DOS_LSR_DATA_READY 0x01
#define DOS_LSR_THR_EMPTY  0x20
#define DOS_LSR_TIMEOUT    0x80

typedef struct {
    int port;
} dos_transport;

wp_status plat_transport_open(const char *endpoint, u32 connect_timeout_ms,
                               plat_transport **out)
{
    dos_transport *t;
    const char *rest;
    const char *colon;
    int port, baud_idx;
    union REGS regs;

    WP_UNUSED(connect_timeout_ms); /* INT 14h's init takes effect
                                       immediately -- no connection
                                       handshake at this layer, same as
                                       transport_atari.c's own Rsconf */

    if (strncmp(endpoint, "serial:", 7) != 0) return WP_UNSUPPORTED;
    rest = endpoint + 7;

    colon = strrchr(rest, ':');
    if (colon == NULL || colon == rest || colon[1] == '\0') return WP_ERR;
    if (!parse_port(rest, colon, &port)) return WP_ERR;
    if (!parse_baud(colon + 1, &baud_idx)) return WP_ERR;

    t = (dos_transport *)mem_alloc((u32)sizeof(dos_transport));
    if (t == NULL) return WP_NOMEM;
    t->port = port;

    /* AL bit layout: baud(7-5) | parity(4-3) | stop(2) | wordlen(1-0).
       8N1 -- parity=00 (none), stop=0 (1 bit), wordlen=11 (8 bits) --
       low 5 bits = 0x03, matching 8N1 the same way transport_atari.c's
       own RS_8BITS|RS_1STOP combination does for Rsconf. */
    regs.h.ah = 0x00;
    regs.h.al = (unsigned char)((baud_idx << 5) | 0x03);
    regs.w.dx = (unsigned short)port;
    int86(0x14, &regs, &regs);

    *out = (plat_transport *)t;
    return WP_OK;
}

/* Polls AH=03h (get status, non-blocking) for DATA_READY up to
   timeout_ms for the FIRST byte (matching platform.h's documented
   contract), then drains whatever else is immediately available (no
   further waiting) up to out_cap via AH=02h -- the same "wait once,
   then drain" shape transport_atari.c's own Bconstat/Bconin pair
   already uses, so callers assembling a multi-byte frame
   (offload/transport/wire.c) need fewer round trips than a strict
   one-byte-per-call implementation would cost them. */
wp_status plat_transport_read(plat_transport *t, u8 *out, u32 out_cap,
                               u32 timeout_ms, u32 *out_len)
{
    dos_transport *dt = (dos_transport *)t;
    u32 deadline;
    u32 n = 0;
    union REGS regs;

    if (out_len) *out_len = 0;
    if (out_cap == 0) return WP_OK;

    deadline = plat_ticks_ms() + timeout_ms;
    for (;;) {
        regs.h.ah = 0x03;
        regs.w.dx = (unsigned short)dt->port;
        int86(0x14, &regs, &regs);
        if (regs.h.ah & DOS_LSR_DATA_READY) break;
        if (plat_ticks_ms() >= deadline) return WP_TIMEOUT;
    }

    while (n < out_cap) {
        regs.h.ah = 0x03;
        regs.w.dx = (unsigned short)dt->port;
        int86(0x14, &regs, &regs);
        if (!(regs.h.ah & DOS_LSR_DATA_READY)) break;

        regs.h.ah = 0x02;
        regs.w.dx = (unsigned short)dt->port;
        int86(0x14, &regs, &regs);
        out[n++] = regs.h.al;
    }

    if (out_len) *out_len = n;
    return WP_OK;
}

/* THR_EMPTY (transmit holding register empty, "ok to send") is the
   transmit-side poll, mirroring DATA_READY's read-side role. Each byte
   gets its own fresh timeout_ms budget, matching transport_atari.c's
   own per-byte Bcostat loop.

   A real, confirmed DOSBox limitation, found during Phase 4 bring-up:
   against DOSBox's default `serial1=dummy` device, AH=03h's status
   poll correctly reports THR_EMPTY (0x60) immediately, but AH=01h
   (send) unconditionally returns AH=0x80 (timeout) regardless --
   reproduced with a minimal, isolated raw INT 14h program bypassing
   this file's own polling logic entirely, and confirmed independent
   of CPU speed (`cpu_cycles`) and of manually asserting DTR/RTS/OUT2
   via a direct Modem Control Register write, so this is not a bug in
   this code or a fixable timing/handshake issue on this side. See
   `docs/dos-platform.md` for the full account and
   `wp_dos_transport_selfcheck`'s own comment for how this is verified
   without depending on DOSBox's send path actually succeeding. */
wp_status plat_transport_write(plat_transport *t, const u8 *data, u32 len,
                                u32 timeout_ms)
{
    dos_transport *dt = (dos_transport *)t;
    u32 i;
    union REGS regs;

    for (i = 0; i < len; i++) {
        u32 deadline = plat_ticks_ms() + timeout_ms;

        for (;;) {
            regs.h.ah = 0x03;
            regs.w.dx = (unsigned short)dt->port;
            int86(0x14, &regs, &regs);
            if (regs.h.ah & DOS_LSR_THR_EMPTY) break;
            if (plat_ticks_ms() >= deadline) return WP_TIMEOUT;
        }

        regs.h.ah = 0x01;
        regs.h.al = data[i];
        regs.w.dx = (unsigned short)dt->port;
        int86(0x14, &regs, &regs);
        if (regs.h.ah & DOS_LSR_TIMEOUT) return WP_ERR;
    }
    return WP_OK;
}

void plat_transport_close(plat_transport *t)
{
    mem_free(t);
}
