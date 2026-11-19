/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/ostruct.h>
#include <mint/mintbind.h>
#include <mint/dcntl.h>
#include <mint/cookie.h>   /* the RSVF cookie, for the faster SCC rates */
#ifdef RP_ATARI_TCP
#include <mint/cookie.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#endif
#include "platform.h"

/* Real RS-232 transport (T6.3) over the ST's AUX BIOS device (DEV_AUX=1,
   the classic Bconin/Bconout/Bconstat/Bcostat convention -- verified
   against mint/ostruct.h, not guessed).

   "tcp:<host>:<port>" is a TCP client over MiNTnet, further down this
   file, and it is OPT-IN AT COMPILE TIME: build with -DRP_ATARI_TCP to
   get it. mintlib's resolver, socket and select code costs a GEM program
   about 68 KB whether or not it ever opens a connection, which is RAM a
   1 MB plain-TOS machine cannot spare for code it cannot run; a program
   that wants the network says so. Without the define, "tcp:" answers
   WP_UNSUPPORTED exactly as it always has. With it, FreeMiNT's socket
   calls (Fsocket et al, GEMDOS 0x160+), which do not exist under plain
   TOS/EmuTOS, are gated at run time on the MiNT cookie: no cookie (or
   MiNT with no network stack loaded) is WP_UNSUPPORTED -- never a crash
   or a hang on a plain machine. Other prefixes are WP_UNSUPPORTED too,
   honestly, not silently accepted. */

/* endpoint format: "serial:<device>:<baud>[:<flow>]" (platform.h's own
   doc comment already establishes the first three fields). <device> is
   currently unused/unvalidated beyond "non-empty" -- the ST has exactly
   one serial port (AUX), so there is nothing to select, but the field is
   kept in the endpoint string for forward compatibility with the
   pipe:/tcp: endpoints' shape.

   BAUD CEILING. 19200 is not a limit this file invented: it is the top
   of Rsconf()'s speed table (BAUD_19200 == 0 in mint/ostruct.h, and the
   XBIOS #15 documentation lists exactly 0=19200 .. 15=50). On the ST
   that ceiling is the MFP 68901's timer-D baud generator. Machines with
   an SCC (Z85C30 -- Mega STE, TT, Falcon) can drive the line far faster,
   but not through this BIOS entry point: reaching those rates needs
   either HSMODEM (which extends the BIOS with additional speed indices)
   or direct SCC register programming (WR12/WR13 time constants), both of
   which are machine-specific and outside the plain-TOS baseline this
   backend targets. So the table below is complete and correct for the
   interface actually being used; a faster link is a separate piece of
   work, not a missing case here. */
static wp_bool parse_baud(const char *s, const char *end, short *out_idx)
{
    long value = 0;
    wp_bool any_digit = WP_FALSE;

    while (s < end && *s >= '0' && *s <= '9') {
        value = value * 10 + (long)(*s - '0');
        /* Bail the moment we exceed the fastest supported rate. Besides
           rejecting junk early, this bounds `value` so a long digit run
           cannot overflow a 32-bit long (signed overflow is undefined,
           and the previous unbounded loop could reach it). */
        if (value > 19200L) return WP_FALSE;
        s++;
        any_digit = WP_TRUE;
    }
    if (!any_digit || s != end) return WP_FALSE;

    switch (value) {
    case 19200: *out_idx = BAUD_19200; return WP_TRUE;
    case 9600:  *out_idx = BAUD_9600;  return WP_TRUE;
    case 4800:  *out_idx = BAUD_4800;  return WP_TRUE;
    case 3600:  *out_idx = BAUD_3600;  return WP_TRUE;
    case 2400:  *out_idx = BAUD_2400;  return WP_TRUE;
    case 2000:  *out_idx = BAUD_2000;  return WP_TRUE;
    case 1800:  *out_idx = BAUD_1800;  return WP_TRUE;
    case 1200:  *out_idx = BAUD_1200;  return WP_TRUE;
    case 600:   *out_idx = BAUD_600;   return WP_TRUE;
    case 300:   *out_idx = BAUD_300;   return WP_TRUE;
    case 200:   *out_idx = BAUD_200;   return WP_TRUE;
    case 150:   *out_idx = BAUD_150;   return WP_TRUE;
    case 134:   *out_idx = BAUD_134;   return WP_TRUE;
    case 110:   *out_idx = BAUD_110;   return WP_TRUE;
    case 75:    *out_idx = BAUD_75;    return WP_TRUE;
    case 50:    *out_idx = BAUD_50;    return WP_TRUE;
    default:    return WP_FALSE;
    }
}

/* Optional 4th endpoint field. OPT-IN ON PURPOSE, and the default stays
   "none" (i.e. exactly the previous behaviour): a great many retro
   serial rigs are wired with a 3-wire cable (TX/RX/GND only). On such a
   cable CTS is never asserted, so with FLOW_HARD selected Bcostat()
   never reports ready and every plat_transport_write would time out --
   a silent default of "hard" would turn a working link into a dead one.
   The caller has to say it wants handshaking, and is thereby asserting
   the cable actually carries RTS/CTS.

   Worth it where the cable does: FLOW_NONE with a polled BIOS AUX device
   has no way to push back on a fast sender, which is survivable for this
   protocol's short CRC-framed request/response traffic (a dropped byte
   costs one frame, and wire.c resyncs) but not for sustained bulk. */
/* "soft" and "both" are DELIBERATELY ABSENT, and their absence is a
   correctness requirement rather than an unfinished case.

   FLOW_SOFT is XON/XOFF: in-band CTRL-S (0x13) and CTRL-Q (0x11)
   (Compendium p.271, Rsconf's own flow table). This protocol's frames
   are binary -- the 4-byte length, the 2-byte sequence and the 2-byte
   CRC all take arbitrary values, so a payload of 17 bytes puts a literal
   0x11 in the length field and roughly one CRC in every 128 contains an
   0x11 or 0x13 byte. Those bytes would be consumed by the UART as flow
   characters and never reach wire.c, which sees a short frame and a CRC
   mismatch. Silent, intermittent, data-dependent corruption -- strictly
   worse than refusing the endpoint outright, and exactly the kind of
   fault that costs days to chase (see this file's own history).

   RTS/CTS is out-of-band, on its own wires, so it carries no such
   hazard. Only "none" and "hard" are offered. */
static wp_bool parse_flow(const char *s, short *out_flow)
{
    if (strcmp(s, "none") == 0) { *out_flow = FLOW_NONE; return WP_TRUE; }
    if (strcmp(s, "hard") == 0) { *out_flow = FLOW_HARD; return WP_TRUE; }
    return WP_FALSE;
}

/* strrchr() bounded by an explicit end, since the fields of the endpoint
   are not individually NUL-terminated and C89 has no strndup to lean on. */
static const char *last_colon_before(const char *s, const char *end)
{
    const char *found = NULL;
    const char *p;

    for (p = s; p < end; p++) {
        if (*p == ':') found = p;
    }
    return found;
}

/* Under plain TOS, AUX is a single global BIOS device and no per-open
   state beyond a non-NULL handle is strictly needed. Under MiNT there
   IS real state: an open file handle on a device node, which must be
   carried to every read/write and closed on the way out.

   WHY THE NODE EXISTS AT ALL. On a real Falcon030 under FreeMiNT, the
   BIOS path simply does not transmit. Measured on the machine: aux: is
   Bconmap device 7, Bconin over it receives perfectly (255 bytes of a
   known pattern, decoded clean), and Bconout over the SAME device moves
   zero bytes while Bcostat reports not-ready 31806, 36126 and 36072
   times across three different Rsconf configurations. Those three
   numbers landing within 1% of each other is the tell: MiNT intercepts
   the BIOS serial calls and routes them through its own driver, and
   Rsconf never reaches the state that driver is gating on -- so all
   three "different" configurations were the same no-op.

   Opening the device node instead and calling Fwrite moved 9863 bytes
   in six seconds on that same machine, with 9865 arriving at the far
   end (the extra two being unrelated boot noise already on the wire).
   Same port, same cable, same baud -- only the path through the OS
   differs.

   This was NOT a handshake problem, though it spent a long time looking
   like one. TIOCMGET on the node reports CTS=1: the wiring is good and
   the handshake is satisfied. Anything in this file's history implying
   a cable fault is superseded by that measurement. */
/* Every transport this file hands out starts with its kind, so the four
   public functions can tell a serial line from a socket. */
enum { TRANSPORT_SERIAL = 1, TRANSPORT_TCP = 2 };

typedef struct {
    int     kind;      /* TRANSPORT_SERIAL */
    wp_bool opened;
    short   fd;        /* MiNT device-node handle, or -1 for the BIOS path */
    u32     baud;      /* the line's speed now */
    short   flow;      /* FLOW_NONE or FLOW_HARD, as the endpoint said */
    wp_bool fast;      /* AUX is a port the RSVF cookie lists: HSMODEM's
                          (and TOS 5's) higher Rsconf codes work on it */
} atari_transport;

#define ATARI_NO_NODE ((short)-1)

/* Candidate device nodes, most-likely first, tried only after any path
   given in the endpoint's own <device> field.

   Probed rather than assumed, and both the bare and u:-prefixed spellings
   are listed, because on the real Falcon "/dev/modem2" did NOT open while
   "u:/dev/modem2" did. Naming has never been uniform across MiNT versions
   and kernel serial drivers, and guessing here has already cost this
   project real time once (a device 6 that does not exist on this machine
   at all). */
static const char *const atari_serial_nodes[] = {
    "u:/dev/modem2", "/dev/modem2",
    "u:/dev/modem1", "/dev/modem1",
    "u:/dev/serial2", "/dev/serial2",
    "u:/dev/serial1", "/dev/serial1",
    (const char *)0
};

/* Opens one candidate and confirms it is really MiNT's serial driver
   rather than something that merely happens to open.

   TIOCMGET is the confirmation, and it is a deliberate choice: it is the
   modem-status ioctl, so a node that answers it is unambiguously a
   serial device with MiNT's driver behind it. u:/dev/aux, for instance,
   opens perfectly well on the Falcon and then returns -32 here -- taking
   it would have put us straight back on the path that does not
   transmit. */
static short open_serial_node(const char *path)
{
    long h;
    long bits = 0;

    if (path == (const char *)0 || *path == '\0') return ATARI_NO_NODE;

    h = Fopen(path, 2);          /* read/write */
    if (h < 0) return ATARI_NO_NODE;

    if (Fcntl((short)h, (long)&bits, TIOCMGET) < 0) {
        Fclose((short)h);
        return ATARI_NO_NODE;
    }
    return (short)h;
}

/* Whether AUX is a port the RSVF cookie lists (HSMODEM's RSVF_COO.TXT;
   TOS 5.01 sets one for the Falcon's SCC, tos4x A10). Its entries are
   eight bytes -- the port's name, a flag byte, a spare, the BIOS device,
   a spare -- ended by a null name. Rsconf codes 10-13 then mean 230400,
   115200, 57600 and 38400 on that port, not 300 to 134. Without the
   cookie nothing is assumed: a plain ST's MFP tops out at 19200, and
   Bconmap is not even asked (TOS 1 has no XBIOS 44). */
typedef struct {
    const char   *name;
    unsigned char flags, spare1, bios, spare2;
} rsvf_entry;

#define C_RSVF 0x52535646L   /* 'RSVF' */

static wp_bool rsvf_lists_aux(void)
{
    long v = 0;
    const rsvf_entry *e;
    long dev;
    int guard;

    if (Getcookie(C_RSVF, &v) != C_FOUND || v == 0) return WP_FALSE;
    dev = Bconmap(-1);                   /* the device AUX is mapped to now */
    for (e = (const rsvf_entry *)v, guard = 0; e->name != NULL && guard < 32; e++, guard++) {
        if ((long)e->bios == dev) return WP_TRUE;
    }
    return WP_FALSE;
}

static wp_status serial_open(const char *endpoint, u32 connect_timeout_ms,
                             plat_transport **out)
{
    atari_transport *t;
    const char *rest;
    const char *colon;
    const char *baud_end;
    short baud_idx;
    short flow;

    WP_UNUSED(connect_timeout_ms); /* Rsconf takes effect immediately --
                                       there is no connection handshake
                                       at this layer, unlike a TCP dial */

    if (strncmp(endpoint, "serial:", 7) != 0) return WP_UNSUPPORTED;
    rest = endpoint + 7;

    /* The trailing field is consumed as <flow> ONLY if it actually spells
       a flow keyword. That keeps every previously valid endpoint parsing
       to the identical result -- including a <device> containing colons,
       which the old strrchr() form accepted ("serial:a:b:9600" still
       means device "a:b", baud 9600, and not a device "a" with a bogus
       flow of "9600"). */
    flow = FLOW_NONE;
    baud_end = rest + strlen(rest);
    colon = strrchr(rest, ':');
    if (colon != NULL && parse_flow(colon + 1, &flow)) {
        baud_end = colon;
    }

    colon = last_colon_before(rest, baud_end);
    if (colon == NULL || colon == rest) return WP_ERR; /* need a non-empty <device> */
    if (!parse_baud(colon + 1, baud_end, &baud_idx)) return WP_ERR;

    t = (atari_transport *)mem_alloc((u32)sizeof(atari_transport));
    if (t == NULL) return WP_NOMEM;
    t->kind = TRANSPORT_SERIAL;
    t->opened = WP_TRUE;
    t->fd = ATARI_NO_NODE;
    t->flow = flow;
    t->baud = 0;
    {
        const char *d = colon + 1;
        while (d < baud_end) t->baud = t->baud * 10u + (u32)(*d++ - '0');
    }
    t->fast = rsvf_lists_aux();

    /* 8N1, async /16 clock, receiver enabled, flow control as requested
       by the endpoint (FLOW_NONE unless asked otherwise).
       RS_INQUIRE (-1) for tsr/scr means "leave unchanged" -- the same
       -1-means-inquire/no-change idiom mint/ostruct.h uses consistently
       elsewhere (SERIAL_NOCHANGE, DISK_NOCHANGE, PRT_INQUIRE, ...);
       there is no documented transmit-side bit list on this system to
       set explicitly (docs/atari-platform.md). */
    Rsconf(baud_idx, flow,
           (short)(RS_8BITS | RS_1STOP | RS_CLK16),
           RS_RECVENABLE,
           (short)RS_INQUIRE, (short)RS_INQUIRE);

    /* Rsconf above is kept EXACTLY as it was, and runs on every machine
       including the ones that then go on to use a device node. It is
       what configures the hardware (baud, word shape, flow) and it is
       the only configuration path that exists under plain TOS. The node,
       when one is found, changes only how bytes are moved afterwards --
       it is not an alternative way to configure the port, and deciding
       baud twice through two different layers is exactly the kind of
       half-applied state this file has been bitten by before.

       Preferring a caller-named path lets an endpoint address a
       specific port on a machine with more than one ("serial:u:/dev/
       modem1:9600"), which the <device> field previously had no effect
       at all on. A device that is not a path keeps its old meaning of
       "ignored", so every endpoint that worked before still parses and
       behaves identically. */
    {
        int i;
        char devbuf[64];
        u32 devlen = (u32)(colon - rest);

        if (devlen > 0 && devlen < (u32)sizeof(devbuf)) {
            wp_bool looks_like_path = WP_FALSE;
            for (i = 0; i < (int)devlen; i++) {
                devbuf[i] = rest[i];
                if (rest[i] == '/') looks_like_path = WP_TRUE;
            }
            devbuf[devlen] = '\0';
            if (looks_like_path) t->fd = open_serial_node(devbuf);
        }

        for (i = 0; t->fd == ATARI_NO_NODE && atari_serial_nodes[i] != (const char *)0; i++) {
            t->fd = open_serial_node(atari_serial_nodes[i]);
        }
    }

    *out = (plat_transport *)t;
    return WP_OK;
}

/* Bconstat(DEV_AUX) is a non-blocking "is a byte ready" poll; Bconin
   only blocks if called without checking that first. Waits up to
   timeout_ms for the FIRST byte (matching platform.h's documented
   contract), then drains whatever else is immediately available (no
   further waiting) up to out_cap -- Bconin only ever returns one byte
   per call, unlike a socket read(), so draining here means callers
   assembling a multi-byte frame (offload/transport/wire.c) need fewer
   plat_transport_read round trips than a strict one-byte-per-call
   implementation would cost them. */
static wp_status serial_read(plat_transport *t, u8 *out, u32 out_cap,
                             u32 timeout_ms, u32 *out_len)
{
    atari_transport *at = (atari_transport *)t;
    u32 t0;
    u32 n = 0;

    if (out_len) *out_len = 0;
    if (out_cap == 0) return WP_OK;

    t0 = plat_ticks_ms();

    /* Node path: Finstat is the non-blocking "how many bytes are
       waiting" poll, so Fread is only ever asked for bytes that are
       already there and can never block past the caller's deadline.
       That matters more than it looks: a bare Fread on a tty would
       block indefinitely, which platform.h's contract forbids and which
       would hang the UI -- the exact failure the offload rules call out
       ("Timeouts never hang the UI"). */
    if (at != NULL && at->fd != ATARI_NO_NODE) {
        long avail, got;

        for (;;) {
            avail = Finstat(at->fd);
            if (avail > 0) break;
            if (avail < 0) return WP_ERR;
            if (PLAT_TICKS_PASSED(t0, timeout_ms)) return WP_TIMEOUT;
        }
        if (avail > (long)out_cap) avail = (long)out_cap;
        got = Fread(at->fd, avail, out);
        if (got < 0) return WP_ERR;
        if (out_len) *out_len = (u32)got;
        return WP_OK;
    }

    while (Bconstat(DEV_AUX) == 0) {
        if (PLAT_TICKS_PASSED(t0, timeout_ms)) return WP_TIMEOUT;
    }

    while (n < out_cap && Bconstat(DEV_AUX) != 0) {
        out[n++] = (u8)(Bconin(DEV_AUX) & 0xFF);
    }

    if (out_len) *out_len = n;
    return WP_OK;
}

/* Bcostat(DEV_AUX) is the transmit-side non-blocking "ok to send"
   poll, mirroring Bconstat. Each byte gets its own fresh timeout_ms
   budget, matching platform/host/transport_host.c's per-poll-call
   convention for plat_transport_write. */
static wp_status serial_write(plat_transport *t, const u8 *data, u32 len,
                              u32 timeout_ms)
{
    atari_transport *at = (atari_transport *)t;
    u32 i;

    /* Node path: Fwrite may legitimately accept fewer bytes than asked
       when the driver's output buffer is full, so loop on progress. The
       deadline is refreshed whenever bytes actually move, which keeps
       the per-byte budget of the BIOS path below rather than imposing
       one flat timeout on an arbitrarily long frame. A write that
       returns 0 repeatedly is genuinely stalled, and only that runs the
       clock down. */
    if (at != NULL && at->fd != ATARI_NO_NODE) {
        u32 done = 0;
        u32 t0 = plat_ticks_ms();

        while (done < len) {
            long w = Fwrite(at->fd, (long)(len - done), data + done);

            if (w < 0) return WP_ERR;
            if (w == 0) {
                if (PLAT_TICKS_PASSED(t0, timeout_ms)) return WP_TIMEOUT;
                continue;
            }
            done += (u32)w;
            t0 = plat_ticks_ms();
        }
        return WP_OK;
    }

    for (i = 0; i < len; i++) {
        u32 t0 = plat_ticks_ms();

        while (Bcostat(DEV_AUX) == 0) {
            if (PLAT_TICKS_PASSED(t0, timeout_ms)) return WP_TIMEOUT;
        }
        Bconout(DEV_AUX, data[i]);
    }
    return WP_OK;
}

static void serial_close(plat_transport *t)
{
    atari_transport *at = (atari_transport *)t;

    if (at != NULL && at->fd != ATARI_NO_NODE) Fclose(at->fd);
    mem_free(t);
}

/* ------------------------------------------------------------------ */
/* tcp:<host>:<port> over MiNTnet                                       */
/* ------------------------------------------------------------------ */

#ifdef RP_ATARI_TCP
/* MiNT's own sockets, through mintlib's BSD calls -- which exist in the
   library on any build but in the system only under MiNT, so the MiNT
   cookie is the gate and nothing socket-shaped is called without it. The
   socket is non-blocking and every wait is a select() with the caller's
   timeout. A dotted address ("192.168.1.10") is taken as it is; only a
   real name goes to the resolver, and THAT CAN BLOCK past any timeout
   (gethostbyname has none) when the configured DNS server is
   unreachable. A peer that has gone away makes send() raise SIGPIPE,
   whose default action under MiNT ends the program, and mintlib has no
   MSG_NOSIGNAL: so the first connection sets SIGPIPE to ignored if it is
   still at its default (a program's own handler is left alone), and the
   write sees EPIPE -- WP_ERR, as platform.h promises. (STinG, the other
   TCP stack an ST may run, has an API of its own and is not here yet.) */
typedef struct {
    int kind;          /* TRANSPORT_TCP */
    int fd;
} atari_tcp;

static wp_bool have_mint(void)
{
    long v = 0;

    return Getcookie(C_MiNT, &v) == C_FOUND;
}

/* Wait until fd is readable (want_write 0) or writable (1), up to ms.
   1 ready, 0 timed out, -1 failed. */
static int tcp_wait(int fd, int want_write, u32 ms)
{
    u32 t0 = plat_ticks_ms();

    for (;;) {
        fd_set set;
        struct timeval tv;
        u32 left = ms - PLAT_TICKS_SINCE(t0);
        int r;

        if (PLAT_TICKS_PASSED(t0, ms)) left = 0;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        tv.tv_sec = (long)(left / 1000u);
        tv.tv_usec = (long)((left % 1000u) * 1000u);
        r = select(fd + 1, want_write ? NULL : &set, want_write ? &set : NULL, NULL, &tv);
        if (r < 0 && errno == EINTR && !PLAT_TICKS_PASSED(t0, ms))
            continue;               /* a signal: wait out what is left */
        return r > 0 ? 1 : (r == 0 ? 0 : -1);
    }
}

static void ignore_sigpipe_once(void)
{
    static wp_bool done = WP_FALSE;
    void (*old)(int);

    if (done) return;
    done = WP_TRUE;
    old = signal(SIGPIPE, SIG_IGN);
    if (old != SIG_DFL && old != SIG_ERR)
        signal(SIGPIPE, old);       /* the program has its own: keep it */
}

static wp_status tcp_open(const char *hostport, u32 connect_timeout_ms,
                          plat_transport **out)
{
    char host[128];
    const char *colon = strrchr(hostport, ':');
    struct hostent *he;
    struct sockaddr_in sa;
    atari_tcp *t;
    long port = 0;
    const char *p;
    size_t n;
    int err = 0;
    socklen_t errlen = (socklen_t)sizeof(err);

    if (!have_mint()) return WP_UNSUPPORTED;
    memset(&sa, 0, sizeof(sa));
    if (colon == NULL || colon == hostport) return WP_ERR;
    n = (size_t)(colon - hostport);
    if (n >= sizeof(host)) return WP_ERR;
    memcpy(host, hostport, n);
    host[n] = '\0';
    for (p = colon + 1; *p >= '0' && *p <= '9' && port <= 65535L; p++)
        port = port * 10 + (*p - '0');
    if (*p != '\0' || port <= 0 || port > 65535L) return WP_ERR;

    {
        /* numbers first: a LAN address never touches the resolver */
        unsigned long a = inet_addr(host);

        if (a == (unsigned long)INADDR_NONE) {
            he = gethostbyname(host);
            if (he == NULL || he->h_length != 4) return WP_ERR;
            memcpy(&a, he->h_addr, 4);
        }
        memcpy(&sa.sin_addr, &a, 4);
    }

    t = (atari_tcp *)mem_alloc((u32)sizeof(atari_tcp));
    if (t == NULL) return WP_NOMEM;
    t->kind = TRANSPORT_TCP;
    t->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (t->fd < 0) {
        mem_free(t);
        /* MiNT without a network stack loaded */
        return errno == ENOSYS || errno == EINVAL ? WP_UNSUPPORTED : WP_ERR;
    }
    fcntl(t->fd, F_SETFL, fcntl(t->fd, F_GETFL, 0) | O_NONBLOCK);
    ignore_sigpipe_once();

    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    if (connect(t->fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        if (errno != EINPROGRESS ||
            tcp_wait(t->fd, 1, connect_timeout_ms) != 1 ||
            getsockopt(t->fd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0 ||
            err != 0) {
            close(t->fd);
            mem_free(t);
            return WP_ERR;
        }
    }
    *out = (plat_transport *)t;
    return WP_OK;
}

/* platform.h: wait up to timeout_ms for the first byte, then hand back
   what is there; bytes sent before a close come first (recv returns them
   before it returns 0), and WP_ERR only once none are left. */
static wp_status tcp_read(atari_tcp *t, u8 *out, u32 out_cap, u32 timeout_ms,
                          u32 *out_len)
{
    int w = tcp_wait(t->fd, 0, timeout_ms);
    long n;

    if (w == 0) return WP_TIMEOUT;
    if (w < 0) return WP_ERR;
    n = recv(t->fd, out, (size_t)out_cap, 0);
    if (n < 0) return errno == EAGAIN ? WP_TIMEOUT : WP_ERR;
    if (n == 0) return WP_ERR;          /* closed, and nothing left */
    if (out_len) *out_len = (u32)n;
    return WP_OK;
}

/* platform.h: WP_TIMEOUT means no progress for timeout_ms -- each wait
   starts again after bytes go out. */
static wp_status tcp_write(atari_tcp *t, const u8 *data, u32 len, u32 timeout_ms)
{
    u32 done = 0;

    while (done < len) {
        int w = tcp_wait(t->fd, 1, timeout_ms);
        long n;

        if (w == 0) return WP_TIMEOUT;
        if (w < 0) return WP_ERR;
        n = send(t->fd, data + done, (size_t)(len - done), 0);
        if (n < 0) {
            if (errno == EAGAIN) continue;
            return WP_ERR;
        }
        done += (u32)n;
    }
    return WP_OK;
}
#endif /* RP_ATARI_TCP */

/* ------------------------------------------------------------------ */
/* Line speed (platform.h)                                               */
/* ------------------------------------------------------------------ */

/* Fastest first. The MFP's own ceiling is 19200 (Timer D); the RSVF
   rates need the cookie, and the two fastest need RTS/CTS as well --
   the endpoint's ":hard" -- because at 115200 the SCC's buffer fills
   faster than a busy machine empties it, and without the handshake the
   overflow is lost (tos4x measured 4 bytes in 900 under Hatari). The
   MiNT device-node path offers nothing: its speed is the driver's
   (TIOCOBAUD), not Rsconf's, and that is not done yet. */
typedef struct { u32 baud; short code; unsigned char needs_rsvf, needs_hard; } atari_rate;
static const atari_rate k_atari_rates[] = {
    { 230400uL, 10, 1, 1 },
    { 115200uL, 11, 1, 1 },
    {  57600uL, 12, 1, 0 },
    {  38400uL, 13, 1, 0 },
    {  19200uL, BAUD_19200, 0, 0 },
    {   9600uL, BAUD_9600, 0, 0 }
};
#define ATARI_RATE_COUNT ((u32)(sizeof(k_atari_rates) / sizeof(k_atari_rates[0])))

static wp_bool rate_ok(const atari_transport *at, const atari_rate *r)
{
    if (r->needs_rsvf && !at->fast) return WP_FALSE;
    if (r->needs_hard && at->flow != FLOW_HARD) return WP_FALSE;
    return WP_TRUE;
}

u32 plat_transport_speeds(plat_transport *t, u32 *rates, u32 cap, u32 *current)
{
    atari_transport *at = (atari_transport *)t;
    u32 i, n = 0;

    if (current) *current = 0;
#ifdef RP_ATARI_TCP
    if (t != NULL && *(int *)t == TRANSPORT_TCP) return 0;
#endif
    if (at == NULL || at->fd != ATARI_NO_NODE || at->baud == 0) return 0;
    if (current) *current = at->baud;
    for (i = 0; i < ATARI_RATE_COUNT && n < cap; i++)
        if (rate_ok(at, &k_atari_rates[i])) rates[n++] = k_atari_rates[i].baud;
    return n;
}

wp_status plat_transport_set_speed(plat_transport *t, u32 baud)
{
    atari_transport *at = (atari_transport *)t;
    const atari_rate *r = NULL;
    _IOREC *io;
    u32 i, t0;

#ifdef RP_ATARI_TCP
    if (t != NULL && *(int *)t == TRANSPORT_TCP) return WP_UNSUPPORTED;
#endif
    if (at == NULL || at->fd != ATARI_NO_NODE || at->baud == 0) return WP_UNSUPPORTED;
    for (i = 0; i < ATARI_RATE_COUNT; i++)
        if (k_atari_rates[i].baud == baud && rate_ok(at, &k_atari_rates[i])) r = &k_atari_rates[i];
    if (r == NULL) return WP_RANGE;

    /* What is written goes out first, at the old speed: AUX's output
       buffer empties (Iorec's second record), then a character's time
       more for the one in the shifter. */
    io = (_IOREC *)Iorec(0);
    t0 = plat_ticks_ms();
    while (io[1].ibufhd != io[1].ibuftl && !PLAT_TICKS_PASSED(t0, 2000u)) { }
    t0 = plat_ticks_ms();
    while (!PLAT_TICKS_PASSED(t0, 20u)) { }

    Rsconf(r->code, at->flow, (short)RS_INQUIRE, (short)RS_INQUIRE,
           (short)RS_INQUIRE, (short)RS_INQUIRE);
    at->baud = baud;
    return WP_OK;
}

/* ------------------------------------------------------------------ */
/* The four functions platform.h declares: by the endpoint's prefix,     */
/* then by the handle's kind.                                           */
/* ------------------------------------------------------------------ */

wp_status plat_transport_open(const char *endpoint, u32 connect_timeout_ms,
                               plat_transport **out)
{
#ifdef RP_ATARI_TCP
    if (strncmp(endpoint, "tcp:", 4) == 0)
        return tcp_open(endpoint + 4, connect_timeout_ms, out);
#endif
    return serial_open(endpoint, connect_timeout_ms, out);
}

wp_status plat_transport_read(plat_transport *t, u8 *out, u32 out_cap,
                               u32 timeout_ms, u32 *out_len)
{
    if (out_len) *out_len = 0;
#ifdef RP_ATARI_TCP
    if (t != NULL && *(int *)t == TRANSPORT_TCP) {
        if (out_cap == 0) return WP_OK;
        return tcp_read((atari_tcp *)t, out, out_cap, timeout_ms, out_len);
    }
#endif
    return serial_read(t, out, out_cap, timeout_ms, out_len);
}

wp_status plat_transport_write(plat_transport *t, const u8 *data, u32 len,
                                u32 timeout_ms)
{
#ifdef RP_ATARI_TCP
    if (t != NULL && *(int *)t == TRANSPORT_TCP)
        return tcp_write((atari_tcp *)t, data, len, timeout_ms);
#endif
    return serial_write(t, data, len, timeout_ms);
}

void plat_transport_close(plat_transport *t)
{
#ifdef RP_ATARI_TCP
    if (t != NULL && *(int *)t == TRANSPORT_TCP) {
        close(((atari_tcp *)t)->fd);
        mem_free(t);
        return;
    }
#endif
    serial_close(t);
}
