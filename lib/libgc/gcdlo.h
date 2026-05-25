#ifndef GCDLO_H
#define GCDLO_H

#include "gc.h"
#include "libdlo.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

GCBOOL GCDisplayLinkCreate(
    GC *pGC,
    dlo_dev_t uid);

void GCDisplayLinkShutDown(GC *pGC);

#ifdef __cplusplus
}
#endif

#endif
