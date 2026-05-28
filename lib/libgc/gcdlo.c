#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "gc.h"
#include "libdlo.h"

#define DLO_HANDLE(bitmap) ((dlo_dev_t)(uintptr_t)((bitmap)->handle))

#ifndef PICOGRAPH_DISPLAYLINK_WIDTH
#define PICOGRAPH_DISPLAYLINK_WIDTH 720
#endif

#ifndef PICOGRAPH_DISPLAYLINK_HEIGHT
#define PICOGRAPH_DISPLAYLINK_HEIGHT 400
#endif

#ifndef PICOGRAPH_DISPLAYLINK_REFRESH
#define PICOGRAPH_DISPLAYLINK_REFRESH 70
#endif

static dlo_mode_t *DLO_ModeForBitmap(GCBITMAP *pBitmap)
{
    if (!pBitmap || !pBitmap->handle) {
        return NULL;
    }
    return dlo_get_mode(DLO_HANDLE(pBitmap));
}

static GCBOOL DLO_BitmapIsDevice(GCBITMAP *pBitmap)
{
    return pBitmap &&
           (pBitmap->disposition == GCDeviceDisplayOnly ||
            pBitmap->disposition == GCDeviceMemory);
}

static GCBOOL DLO_ViewForBitmap(GCBITMAP *pBitmap, dlo_view_t *view)
{
    dlo_mode_t *pMode = DLO_ModeForBitmap(pBitmap);
    if (!pMode || !view || !pMode->view.width || !pMode->view.height) {
        return GCFALSE;
    }

    *view = pMode->view;
    view->base = (dlo_ptr_t)pBitmap->deviceBase;
    return GCTRUE;
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

void DLO_HWBeginAccess(void *hDevice)
{
}

void DLO_HWEndAccess(void *hDevice)
{
    GCBITMAP *pBitmap = (GCBITMAP*)hDevice;
    if (!pBitmap || !pBitmap->handle || pBitmap->disposition != GCDeviceDisplayOnly) {
        return;
    }
    DLO_CheckRetcode("dlo_flush_usb", dlo_flush_usb(DLO_HANDLE(pBitmap), true));
}

void DLO_HWColorFill(GCBITMAP *pBitmap, long x, long y, long cx, long cy, GCCOLOR color)
{
    dlo_view_t view;
    dlo_rect_t rec;
    dlo_retcode_t ret;

    if (cx <= 0 || cy <= 0) {
        return;
    }

    if (!DLO_BitmapIsDevice(pBitmap) || !DLO_ViewForBitmap(pBitmap, &view)) {
        return;
    }

    if (x >= 0 && y >= 0 &&
        (uint32_t)x < view.width && (uint32_t)y < view.height &&
        (uint32_t)cx <= (uint32_t)view.width - (uint32_t)x &&
        (uint32_t)cy <= (uint32_t)view.height - (uint32_t)y) {
        ret = dlo_fill_rect_unclipped(DLO_HANDLE(pBitmap),
                                      &view,
                                      (int32_t)x,
                                      (int32_t)y,
                                      (uint32_t)cx,
                                      (uint32_t)cy,
                                      color);
        DLO_CheckRetcode("dlo_fill_rect_unclipped", ret);
        return;
    }

    rec.height = cy;
    rec.width = cx;
    rec.origin.x = x;
    rec.origin.y = y;

    ret = dlo_fill_rect(DLO_HANDLE(pBitmap), &view, &rec, color);
    DLO_CheckRetcode("dlo_fill_rect", ret);
}

void DLO_HWSetPixel(GCBITMAP *pBitmap, long x, long y, GCCOLOR color)
{
    DLO_HWColorFill(pBitmap, x, y, 1, 1, color);
}

GCCOLOR DLO_HWGetPixel(GCBITMAP *pBitmap, long x, long y)
{
    (void)pBitmap;
    (void)x;
    (void)y;
    return 0;
}

void DLO_HWCopyBits(GCBITMAP *pBitmap,
                    long x,
                    long y,
                    long cx,
                    long cy,
                    GCBITMAP *pBitmapSrc,
                    long xSrc,
                    long ySrc,
                    long cxSrc,
                    long cySrc,
                    GCMATRIX *pMatrix)
{
    dlo_view_t dest_view;
    dlo_retcode_t ret;
    long width = cx < cxSrc ? cx : cxSrc;
    long height = cy < cySrc ? cy : cySrc;

    (void)pMatrix;

    if (!DLO_BitmapIsDevice(pBitmap) ||
        !pBitmapSrc ||
        !DLO_ViewForBitmap(pBitmap, &dest_view) ||
        width <= 0 ||
        height <= 0) {
        return;
    }

    if (DLO_BitmapIsDevice(pBitmapSrc)) {
        dlo_view_t src_view;
        dlo_rect_t src_rec;
        dlo_dot_t dest_pos;

        if (!DLO_ViewForBitmap(pBitmapSrc, &src_view)) {
            return;
        }

        if (xSrc >= 0 && ySrc >= 0 && x >= 0 && y >= 0 &&
            (uint32_t)xSrc < src_view.width &&
            (uint32_t)ySrc < src_view.height &&
            (uint32_t)x < dest_view.width &&
            (uint32_t)y < dest_view.height &&
            (uint32_t)width <= (uint32_t)src_view.width - (uint32_t)xSrc &&
            (uint32_t)height <= (uint32_t)src_view.height - (uint32_t)ySrc &&
            (uint32_t)width <= (uint32_t)dest_view.width - (uint32_t)x &&
            (uint32_t)height <= (uint32_t)dest_view.height - (uint32_t)y) {
            ret = dlo_copy_rect_unclipped(DLO_HANDLE(pBitmap),
                                          &src_view,
                                          &dest_view,
                                          (int32_t)xSrc,
                                          (int32_t)ySrc,
                                          (uint32_t)width,
                                          (uint32_t)height,
                                          (int32_t)x,
                                          (int32_t)y);
            DLO_CheckRetcode("dlo_copy_rect_unclipped", ret);
            return;
        }

        src_rec.origin.x = xSrc;
        src_rec.origin.y = ySrc;
        src_rec.width = width;
        src_rec.height = height;
        dest_pos.x = x;
        dest_pos.y = y;

        ret = dlo_copy_rect(DLO_HANDLE(pBitmap), &src_view, &src_rec, &dest_view, &dest_pos);
        DLO_CheckRetcode("dlo_copy_rect", ret);
        return;
    }

    if (pBitmapSrc->prgBits) {
        const GCCOLOR *pixels;
        dlo_fbuf_t fbuf;
        dlo_dot_t pos;
        dlo_bmpflags_t flags = {0};

        if (xSrc < 0 || ySrc < 0 ||
            xSrc >= pBitmapSrc->width ||
            ySrc >= pBitmapSrc->height) {
            return;
        }
        if (width > pBitmapSrc->width - xSrc) {
            width = pBitmapSrc->width - xSrc;
        }
        if (height > pBitmapSrc->height - ySrc) {
            height = pBitmapSrc->height - ySrc;
        }
        if (width <= 0 || height <= 0 ||
            width > UINT16_MAX ||
            height > UINT16_MAX) {
            return;
        }

        pixels = pBitmapSrc->prgBits + (ySrc * pBitmapSrc->width) + xSrc;
        if (height == 1 && sizeof(GCCOLOR) == sizeof(uint32_t) &&
            x >= 0 && y >= 0 &&
            (uint32_t)x < dest_view.width &&
            (uint32_t)y < dest_view.height &&
            (uint32_t)width <= (uint32_t)dest_view.width - (uint32_t)x) {
            ret = dlo_copy_rgbx8888_line(DLO_HANDLE(pBitmap),
                                         &dest_view,
                                         (int32_t)x,
                                         (int32_t)y,
                                         (const uint32_t *)pixels,
                                         (uint32_t)width);
            DLO_CheckRetcode("dlo_copy_rgbx8888_line", ret);
            return;
        }

        fbuf.width = (uint16_t)width;
        fbuf.height = (uint16_t)height;
        fbuf.fmt = dlo_pixfmt_abgr8888;
        fbuf.base = (void *)pixels;
        fbuf.stride = (uint32_t)pBitmapSrc->width;
        pos.x = x;
        pos.y = y;

        ret = dlo_copy_host_bmp(DLO_HANDLE(pBitmap), flags, &fbuf, &dest_view, &pos);
        DLO_CheckRetcode("dlo_copy_host_bmp", ret);
    }
}

unsigned long DLO_HWFrameBytes(GCBITMAP *pBitmap)
{
    dlo_mode_t *pMode;
    uint32_t bytes_per_pixel;

    pMode = DLO_ModeForBitmap(pBitmap);
    if (!pMode || !pMode->view.width || !pMode->view.height) {
        return 0;
    }

    if (pMode->view.bpp == 24) {
        bytes_per_pixel = 3;
    } else if ((pMode->view.bpp % 8) == 0) {
        bytes_per_pixel = pMode->view.bpp / 8;
    } else {
        return 0;
    }

    return (unsigned long)pMode->view.width *
           (unsigned long)pMode->view.height *
           (unsigned long)bytes_per_pixel;
}

GCBOOL DLO_HWPresentBase(GCBITMAP *pBitmap, unsigned long base)
{
    dlo_mode_t *pMode;
    dlo_retcode_t ret;

    if (!pBitmap || !pBitmap->handle || (base & 1u)) {
        return GCFALSE;
    }

    pMode = DLO_ModeForBitmap(pBitmap);
    if (!pMode) {
        return GCFALSE;
    }

    if (pMode->view.base != (dlo_ptr_t)base) {
        ret = dlo_set_view_base(DLO_HANDLE(pBitmap), (dlo_ptr_t)base);
        if (!DLO_CheckRetcode("dlo_set_view_base", ret)) {
            return GCFALSE;
        }
    }
    return GCTRUE;
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

    pMode = dlo_get_mode(uid);

    if (!pMode) {
        printf("gc: dlo_get_mode failed to get native mode\n");
        return GCFALSE;
    }

    if (pMode->view.width == 800 && pMode->view.height == 480) {
        printf("gc: DisplayLink skipping mode set for LCD panel, centering view.\n");
        long xOffset = (pMode->view.width - PICOGRAPH_DISPLAYLINK_WIDTH) / 2;
        long yOffset = (pMode->view.height - PICOGRAPH_DISPLAYLINK_HEIGHT) / 2;
        GCSetOffset(pDevice->pDisplay, xOffset, yOffset);
    } else {
        desc.view.base = 0;
        desc.view.width = PICOGRAPH_DISPLAYLINK_WIDTH;
        desc.view.height = PICOGRAPH_DISPLAYLINK_HEIGHT;
        desc.view.bpp = 24;
        desc.refresh = PICOGRAPH_DISPLAYLINK_REFRESH;

        ret = dlo_set_mode(uid, &desc);

        if (!DLO_CheckRetcode("dlo_set_mode", ret)) {
            printf("gc: DisplayLink mode not accepted, ignoring.\n");
        }

        pMode = dlo_get_mode(uid);

        if (!pMode) {
            printf("gc: dlo_get_mode failed after successful mode set\n");
            return GCFALSE;
        }
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

    pDisplaySurface->pGC = pDevice->pDisplay;
    GCInitializeBitmap(pDisplaySurface,
                       pMode->view.width,
                       pMode->view.height,
                       (unsigned long)(uintptr_t)uid,
                       GCDeviceDisplayOnly);

    pDevice->HWBeginAccess =        (HWBEGINACCESS) DLO_HWBeginAccess;
    pDevice->HWEndAccess =          (HWENDACCESS) DLO_HWEndAccess;
    pDevice->HWGetPixel =           (HWGETPIXEL) DLO_HWGetPixel;
    pDevice->HWSetPixel =           (HWSETPIXEL) DLO_HWSetPixel;
    pDevice->HWColorFill =          (HWCOLORFILL)DLO_HWColorFill;
    pDevice->HWDrawLine =           (HWDRAWLINE) NULL;
    pDevice->HWCopyBits =           (HWCOPYBITS) DLO_HWCopyBits;
    pDevice->HWConstantAlphaBlend = (HWCONSTANTALPHABLEND)NULL;
    pDevice->HWTransparentBlt =     (HWTRANSPARENTBLT)NULL;
    pDevice->HWDimBuffer =          (HWDIMBUFFER)NULL;
    pDevice->HWFrameBytes =         (HWFRAMEBYTES)DLO_HWFrameBytes;
    pDevice->HWPresentBase =        (HWPRESENTBASE)DLO_HWPresentBase;
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
    dlo_dev_t uid,
    uint8_t dev_addr)
{
    return GCInitialize((HWSETUPDEVICE)DLO_HWSetupDevice, uid, &gcDisplay);
}

void GCDisplayLinkShutDown (uint8_t dev_addr)
{
    GC *pGC = &gcDisplay;
    GCDEVICE* pDevice = &pGC->device;
    GCBITMAP *pDisplaySurface = &pGC->bitmap;

    dlo_release_device(DLO_HANDLE(pDisplaySurface));

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
    pDevice->HWFrameBytes =         (HWFRAMEBYTES)NULL;
    pDevice->HWPresentBase =        (HWPRESENTBASE)NULL;
    pDevice->hDevice = NULL;

    pDisplaySurface->disposition = GCDeviceDisplayOnly;
    pDisplaySurface->width = 0;
    pDisplaySurface->height = 0;
    pDisplaySurface->deviceBase = 0;
    pDisplaySurface->handle = 0;
    pDisplaySurface->pGC = pDevice->pDisplay;
    pDisplaySurface->pGCPalette = NULL;
    pDisplaySurface->pDevice = pDevice;
    pDisplaySurface->entryLevel = 0;
}

PGC GCDisplay(void)
{
    return &gcDisplay;
}

