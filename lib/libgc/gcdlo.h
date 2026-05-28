#ifndef GCDLO_H
#define GCDLO_H

#include "gc.h"
#include "libdlo.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

GCBOOL GCDisplayLinkCreate(
    dlo_dev_t uid,
    uint8_t dev_addr);

void GCDisplayLinkShutDown(
    uint8_t dev_addr);

#ifdef __cplusplus
}
#endif

#endif
