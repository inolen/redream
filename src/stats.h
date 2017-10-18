#ifndef STATS_H
#define STATS_H

#include "core/profiler.h"

DECLARE_COUNTER(frames);
DECLARE_COUNTER(aica_samples);
DECLARE_COUNTER(arm7_instrs);
DECLARE_COUNTER(pvr_vblanks);
DECLARE_COUNTER(ta_renders);
DECLARE_COUNTER(sh4_instrs);
DECLARE_COUNTER(mmio_read);
DECLARE_COUNTER(mmio_write);

#endif
