#include <stdio.h>
#include "pico/stdlib.h"
#include "gc.h"
#include "libdlo.h"

#define DLO_HANDLE(bitmap) ((dlo_dev_t)(uintptr_t)((bitmap)->handle))

long g_DLOScaleFactor = 1;
static long DLO_ScaleFactor(void)
{
    return g_DLOScaleFactor;
}

static GCBOOL DLO_CheckRetcode(const char *operation, dlo_retcode_t ret)
{
    if (ret == dlo_ok) {
        return GCTRUE;
    }

    if (ret >= dlo_warn_dl160_mode && ret < dlo_user_example) {
        return GCTRUE;
    }

    printf("gc: %s failed: %s (%lu)\n",
           operation,
           dlo_strerror(ret),
           (unsigned long)ret);
    return GCFALSE;
}

static void DLO_ClearDevice(GCDEVICE *pDevice, GCBITMAP *pDisplaySurface)
{
    pDevice->HWBeginAccess =        (HWBEGINACCESS)NULL;
    pDevice->HWEndAccess =          (HWENDACCESS)NULL;
    pDevice->HWGetPixel =           (HWGETPIXEL)NULL;
    pDevice->HWSetPixel =           (HWSETPIXEL)NULL;
    pDevice->HWColorFill =          (HWCOLORFILL)NULL;
    pDevice->HWDrawLine =           (HWDRAWLINE)NULL;
    pDevice->HWCopyBits =           (HWCOPYBITS)NULL;
    pDevice->HWConstantAlphaBlend = (HWCONSTANTALPHABLEND)NULL;
    pDevice->HWTransparentBlt =     (HWTRANSPARENTBLT)NULL;
    pDevice->HWDimBuffer =          (HWDIMBUFFER)NULL;
    pDevice->hDevice = NULL;

    pDisplaySurface->disposition = GCDeviceDisplayOnly;
    pDisplaySurface->width = 0;
    pDisplaySurface->height = 0;
    pDisplaySurface->handle = 0;
    pDisplaySurface->pGC = pDevice->pDisplay;
    pDisplaySurface->pGCPalette = NULL;
    pDisplaySurface->pDevice = pDevice;
    pDisplaySurface->entryLevel = 0;
}

void DLO_HWBeginAccess(void *hDevice)
{
}

void DLO_HWEndAccess(void *hDevice)
{
    GCBITMAP *pBitmap = (GCBITMAP*)hDevice;
    dlo_flush_usb(DLO_HANDLE(pBitmap), false);
}

void DLO_HWSetPixel(GCBITMAP *pBitmap, long x, long y, GCCOLOR color)
{
    long scale = DLO_ScaleFactor();
    dlo_rect_t rec;
    rec.height = scale;
    rec.width = scale;
    rec.origin.x = x * scale;
    rec.origin.y = y * scale;

    // Frame buffer
    if (pBitmap->disposition == GCDeviceDisplayOnly)
    {
        dlo_fill_rect(DLO_HANDLE(pBitmap),
                      &dlo_get_mode(DLO_HANDLE(pBitmap))->view,
                      &rec,
                      color);
    }
}

GCCOLOR DLO_HWGetPixel(GCBITMAP *pBitmap, long x, long y)
{
}

void DLO_HWColorFill(GCBITMAP *pBitmap, long x, long y, long cx, long cy, GCCOLOR color)
{
    long scale = DLO_ScaleFactor();
    dlo_rect_t rec;
    if (cx <= 0 || cy <= 0) {
        return;
    }
    rec.height = cy * scale;
    rec.width = cx * scale;
    rec.origin.x = x * scale;
    rec.origin.y = y * scale;

    // Frame buffer
    if (pBitmap->disposition == GCDeviceDisplayOnly)
    {
        dlo_fill_rect(DLO_HANDLE(pBitmap),
                      &dlo_get_mode(DLO_HANDLE(pBitmap))->view,
                      &rec,
                      color);
    }
}

GCBOOL DLO_HWSetupDevice(GCDEVICE *pDevice, GCBITMAP *pDisplaySurface, void *param)
{
    dlo_mode_t desc = {0};
    dlo_retcode_t ret;
    dlo_mode_t *pMode;
    dlo_dev_t uid = (dlo_dev_t)param;

    if (!pDevice->pDisplay) {
        printf("gc: DisplayLink setup missing parent GC\n");
        return GCFALSE;
    }

    ret = dlo_set_mode(uid, &desc);
    if (!DLO_CheckRetcode("dlo_set_mode", ret)) {
        return GCFALSE;
    }

    pMode = dlo_get_mode(uid);
    if (!pMode) {
        printf("gc: dlo_get_mode failed after successful mode set\n");
        return GCFALSE;
    }

    if (!pMode->view.width || !pMode->view.height) {
        printf("gc: DisplayLink active mode has invalid size %ux%u\n",
               pMode->view.width,
               pMode->view.height);
        return GCFALSE;
    }

    if (pMode->view.bpp != 24) {
        printf("gc: DisplayLink active mode has unsupported bpp %u\n",
               pMode->view.bpp);
        return GCFALSE;
    }

    g_DLOScaleFactor = 2;
    pDisplaySurface->pGC = pDevice->pDisplay;
    GCInitializeBitmap(pDisplaySurface,
                       pMode->view.width / g_DLOScaleFactor,
                       pMode->view.height / g_DLOScaleFactor,
                       (unsigned long)(uintptr_t)uid,
                       GCDeviceDisplayOnly);

    pDevice->HWBeginAccess =        (HWBEGINACCESS) DLO_HWBeginAccess;
    pDevice->HWEndAccess =          (HWENDACCESS) DLO_HWEndAccess;
    pDevice->HWGetPixel =           (HWGETPIXEL) DLO_HWGetPixel;
    pDevice->HWSetPixel =           (HWSETPIXEL) DLO_HWSetPixel;
    pDevice->HWColorFill =          (HWCOLORFILL)DLO_HWColorFill;
    pDevice->HWDrawLine =           (HWDRAWLINE) NULL;
    pDevice->HWCopyBits =           (HWCOPYBITS) NULL;
    pDevice->HWConstantAlphaBlend = (HWCONSTANTALPHABLEND)NULL;
    pDevice->HWTransparentBlt =     (HWTRANSPARENTBLT)NULL;
    pDevice->HWDimBuffer =          (HWDIMBUFFER)NULL;
    pDevice->hDevice = pDisplaySurface;

    printf("gc: displaylink %ux%u %u bpp @ %u Hz\n",
           pMode->view.width,
           pMode->view.height,
           pMode->view.bpp,
           pMode->refresh);
    return GCTRUE;
}

static GC gcDisplay;

GCBOOL GCDisplayLinkCreate (
    GC *pGC,
    dlo_dev_t uid)
{
    return GCInitialize((HWSETUPDEVICE)DLO_HWSetupDevice, uid, pGC);
}

void GCDisplayLinkShutDown (GC *pGC)
{
    // Nothing to free yet.
}

PGC GCDisplay(void)
{
    return &gcDisplay;
}

void dlo_device_configured (dlo_dev_t uid)
{
    printf("gc: dlo device configured\n");
    if (!GCDisplayLinkCreate(&gcDisplay, uid)) {
        printf("gc: displaylink initialization failed\n");
    }
}
