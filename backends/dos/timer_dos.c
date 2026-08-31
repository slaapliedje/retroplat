#include <time.h>
#include "platform.h"

/* Open Watcom's own DOS libc clock() reads the BIOS system tick (INT
   1Ah, ~18.2Hz) -- no raw interrupt calls needed, the same "just use
   the cross-compiler's own libc" shape platform/atari/timer_atari.c
   already uses for mintlib's own clock(). Integer math only, same
   reasoning as that file's own comment. */

u32 plat_ticks_ms(void)
{
    clock_t c = clock();

    return (u32)(((u32)c * 1000UL) / (u32)CLOCKS_PER_SEC);
}

void plat_sleep_ms(u32 ms)
{
    clock_t start = clock();
    clock_t target_ticks = (clock_t)(((u32)ms * (u32)CLOCKS_PER_SEC) / 1000UL);

    while ((clock() - start) < target_ticks) {
        /* Busy-wait: no DOS-native yield is assumed available on plain
           real-mode DOS. Sleep durations in this app are short and rare
           (no offload device is wired up yet -- T14.4 -- so nothing
           currently calls this with a large ms), matching timer_atari.c's
           own identical reasoning. */
    }
}
