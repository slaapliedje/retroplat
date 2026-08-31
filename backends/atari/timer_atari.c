#include <time.h>
#include "platform.h"

/* mintlib's clock() reads the ST's 200Hz system tick. Integer math only
   (no floats) -- CLOCKS_PER_SEC is a small integer constant here, not
   worth pulling in softfloat for. */

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
        /* Busy-wait: no MiNT/FreeMiNT Fsleep()-style yield is assumed
           available on plain TOS. Sleep durations in this app are short
           and rare (no offload device is wired up yet -- T6.3 -- so
           nothing currently calls this with a large ms). */
    }
}
