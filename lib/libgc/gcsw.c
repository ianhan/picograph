#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gc.h"

static GCBOOL SWClipRect(GCBITMAP *pBitmap, long *x, long *y, long *cx, long *cy)
{
    if (!pBitmap || *cx <= 0 || *cy <= 0)
        return GCFALSE;
    if (*x < 0)
    {
        *cx += *x;
        *x = 0;
    }
    if (*y < 0)
    {
        *cy += *y;
        *y = 0;
    }
    if (*x >= pBitmap->width || *y >= pBitmap->height)
        return GCFALSE;
    if (*cx > pBitmap->width - *x)
        *cx = pBitmap->width - *x;
    if (*cy > pBitmap->height - *y)
        *cy = pBitmap->height - *y;
    return (*cx > 0 && *cy > 0) ? GCTRUE : GCFALSE;
}

static GCBOOL SWClipCopy(GCBITMAP *pBitmap, long *x, long *y, long *cx, long *cy,
                         GCBITMAP *pBitmapSource, long *xSrc, long *ySrc,
                         long *cxSrc, long *cySrc)
{
    long clipped;

    if (!pBitmap || !pBitmapSource || *cx <= 0 || *cy <= 0 || *cxSrc <= 0 || *cySrc <= 0)
        return GCFALSE;

    if (*cx > *cxSrc)
        *cx = *cxSrc;
    if (*cy > *cySrc)
        *cy = *cySrc;

    if (*x < 0)
    {
        clipped = -*x;
        *xSrc += clipped;
        *cx -= clipped;
        *x = 0;
    }
    if (*y < 0)
    {
        clipped = -*y;
        *ySrc += clipped;
        *cy -= clipped;
        *y = 0;
    }
    if (*xSrc < 0)
    {
        clipped = -*xSrc;
        *x += clipped;
        *cx -= clipped;
        *xSrc = 0;
    }
    if (*ySrc < 0)
    {
        clipped = -*ySrc;
        *y += clipped;
        *cy -= clipped;
        *ySrc = 0;
    }

    if (*cx <= 0 || *cy <= 0 ||
        *x >= pBitmap->width || *y >= pBitmap->height ||
        *xSrc >= pBitmapSource->width || *ySrc >= pBitmapSource->height)
        return GCFALSE;

    if (*cx > pBitmap->width - *x)
        *cx = pBitmap->width - *x;
    if (*cy > pBitmap->height - *y)
        *cy = pBitmap->height - *y;
    if (*cx > pBitmapSource->width - *xSrc)
        *cx = pBitmapSource->width - *xSrc;
    if (*cy > pBitmapSource->height - *ySrc)
        *cy = pBitmapSource->height - *ySrc;

    *cxSrc = *cx;
    *cySrc = *cy;
    return (*cx > 0 && *cy > 0) ? GCTRUE : GCFALSE;
}

void SWMemSetPixel(GCBITMAP *pBitmap, long x, long y, GCCOLOR color)
{
    GC gcPunt;
    GCBOOL punted = GCFALSE;
    if (pBitmap->pGC == pBitmap->pDevice->pDisplay)
        return;
    if (x >= pBitmap->width || y  >= pBitmap->height || x < 0 || y < 0)
        return;
    if (pBitmap->disposition == GCDeviceMemory)
    {
        GCRECT workarea = {x, y, x+1, y+1};
        if (GCCreatePuntSurface(pBitmap->pGC, &workarea, GCFALSE, &gcPunt) == GCFALSE)
            return;
        pBitmap = &gcPunt.bitmap;
        x = 0;
        y = 0;
        punted = GCTRUE;
    }
    pBitmap->prgBits[((y) * pBitmap->width) + x] = color;
    if (punted)
    {
        GCDelete(&gcPunt);
    }
}

GCCOLOR SWMemGetPixel(GCBITMAP *pBitmap, long x, long y)
{
    GC gcPunt;
    GCBOOL punted = GCFALSE;
    if (pBitmap->pGC == pBitmap->pDevice->pDisplay)
        return 0;
    if (x >= pBitmap->width || y  >= pBitmap->height || x < 0 || y < 0)
        return 0;
    if (pBitmap->disposition == GCDeviceMemory)
    {
        GCRECT workarea = {x, y, x+1, y+1};
        if (GCCreatePuntSurface(pBitmap->pGC, &workarea, GCTRUE, &gcPunt) == GCFALSE)
            return 0;
        pBitmap = &gcPunt.bitmap;
        x = 0;
        y = 0;
        punted = GCTRUE;
    }
    GCCOLOR color = pBitmap->prgBits[((y) * pBitmap->width) + x];
    if (punted)
    {
        gcPunt.pPuntGC = NULL;
        GCDelete(&gcPunt);
    }
    return color;
}

void SWMemCopyBits(GCBITMAP *pBitmap, long x, long y, long cx, long cy, GCBITMAP *pBitmapSource, long xSrc, long ySrc, long cxSrc, long cySrc, GCMATRIX *pMatrix)
{
    GC gcPunt, gcPull;
    GCBOOL punted = GCFALSE;
    GCBOOL pulled = GCFALSE;
    int dy;
    (void)pMatrix;

    if (pBitmap->pGC == pBitmap->pDevice->pDisplay)
        return;
    if (SWClipCopy(pBitmap, &x, &y, &cx, &cy, pBitmapSource, &xSrc, &ySrc, &cxSrc, &cySrc) == GCFALSE)
        return;
    if (pBitmap->disposition == GCDeviceMemory)
    {
        GCRECT workarea = {x, y, x+cx, y+cy};
        if (GCCreatePuntSurface(pBitmap->pGC, &workarea, GCFALSE, &gcPunt) == GCFALSE)
            return;
        pBitmap = &gcPunt.bitmap;
        x = 0;
        y = 0;
        punted = GCTRUE;
    }
    if (pBitmapSource->disposition == GCDeviceMemory ||
        pBitmapSource->disposition == GCDeviceDisplayOnly)
    {
        GCRECT workarea = {xSrc, ySrc, xSrc+cxSrc, ySrc+cySrc};
        if (GCCreatePuntSurface(pBitmapSource->pGC, &workarea, GCTRUE, &gcPull) == GCFALSE)
            return;
        pBitmapSource = &gcPull.bitmap;
        xSrc = 0;
        ySrc = 0;
        pulled = GCTRUE;
    }
    for (dy = 0; dy < cy; dy++)
    {
        GCCOLOR *pSrc = &pBitmapSource->prgBits[(((ySrc + dy)) * pBitmapSource->width) + xSrc];
        GCCOLOR *pDst = &pBitmap->prgBits[(((y + dy)) * pBitmap->width) + x];
        memcpy(pDst, pSrc, cx * sizeof(GCCOLOR));
    }
    if (pulled)
    {
        gcPull.pPuntGC = NULL;
        GCDelete(&gcPull);
    }
    if (punted)
    {
        GCDelete(&gcPunt);
    }
}

void SWMemColorFill(GCBITMAP *pBitmap, long x, long y, long cx, long cy, GCCOLOR color)
{
    GC gcPunt;
    GCBOOL punted = GCFALSE;
    int dy;
    int dx;

    if (pBitmap->pGC == pBitmap->pDevice->pDisplay)
        return;

    if (SWClipRect(pBitmap, &x, &y, &cx, &cy) == GCFALSE)
        return;

    if (pBitmap->disposition == GCDeviceMemory)
    {
        GCRECT workarea = {x, y, x+cx, y+cy};
        if (GCCreatePuntSurface(pBitmap->pGC, &workarea, GCFALSE, &gcPunt) == GCFALSE)
            return;
        pBitmap = &gcPunt.bitmap;
        x = 0;
        y = 0;
        punted = GCTRUE;
    }

    for (dy = 0; dy < cy; dy++)
    {
        GCCOLOR *pDst = &pBitmap->prgBits[((y + dy) * pBitmap->width) + x];
#ifdef GCCOLOR_ONEBYTE
        memset(pDst, color, cx);
#else
        for (dx = 0; dx < cx; dx++)
        {
            pDst[dx] = color;
        }
#endif
    }

    if (punted)
    {
        GCDelete(&gcPunt);
    }
}

void SWBeginAccess(void *hDevice)
{
    (void)hDevice;
}

void SWEndAccess(void *hDevice)
{
    (void)hDevice;
}

GCBOOL SWMemSetupDevice(GCDEVICE *pDevice, GCBITMAP *pDisplaySurface, void *param)
{
    (void)param;

    pDevice->HWBeginAccess        = (HWBEGINACCESS)SWBeginAccess;
    pDevice->HWEndAccess          = (HWENDACCESS)SWEndAccess;
    pDevice->HWGetPixel           = (HWGETPIXEL)SWMemGetPixel;
    pDevice->HWSetPixel           = (HWSETPIXEL)SWMemSetPixel;
    pDevice->HWColorFill          = (HWCOLORFILL)SWMemColorFill;
    pDevice->HWCopyBits           = (HWCOPYBITS)SWMemCopyBits;
    pDevice->HWConstantAlphaBlend = NULL;
    pDevice->HWDimBuffer          = NULL;
    pDevice->HWFrameBytes         = NULL;
    pDevice->HWPresentBase        = NULL;
    pDevice->hDevice              = pDisplaySurface;
    pDevice->entryLevel           = 0;

    // Software device gets an empty framebuffer.
    pDisplaySurface->pGC = pDevice->pDisplay;
    GCInitializeBitmap(pDisplaySurface, 0, 0, 0, GCDeviceMemory);

    return GCTRUE;
}

