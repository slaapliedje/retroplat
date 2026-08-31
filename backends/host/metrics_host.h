#ifndef WP_METRICS_HOST_H
#define WP_METRICS_HOST_H

#include "types.h"

/* Host-only introspection into plat_measure_text call volume, used by
   tests to prove "minimal recompute" acceptance criteria (T3.3: editing
   one paragraph must not re-measure the whole document). This header is
   never included from engine/ or services/ code -- it is not part of
   the portable platform.h surface. */

u32  metrics_host_measure_call_count(void);
void metrics_host_reset_measure_count(void);

#endif /* WP_METRICS_HOST_H */
