#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gc.h"

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
        punted = GCTRUE;
    }
    GCCOLOR color = pBitmap->prgBits[((y) * pBitmap->width) + x];
    if (punted)
    {
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
    if (pBitmap->pGC == pBitmap->pDevice->pDisplay)
        return;
    if (pBitmap->disposition == GCDeviceMemory)
    {
        GCRECT workarea = {x, y, x+cx, y+cy};
        if (GCCreatePuntSurface(pBitmap->pGC, &workarea, GCFALSE, &gcPunt) == GCFALSE)
            return;
        pBitmap = &gcPunt.bitmap;
        punted = GCTRUE;
    }
    if (pBitmapSource->disposition == GCDeviceMemory)
    {
        GCRECT workarea = {xSrc, ySrc, cx, cy};
        if (GCCreatePuntSurface(pBitmapSource->pGC, &workarea, GCFALSE, &gcPull) == GCFALSE)
            return;
        pBitmapSource = &gcPull.bitmap;
        pulled = GCTRUE;
    }
    for (dy = 0; dy < cy; dy++)
    {
        GCCOLOR *pSrc = &pBitmapSource->prgBits[(((ySrc + dy)) * cxSrc) + xSrc];
        GCCOLOR *pDst = &pBitmap->prgBits[(((y + dy)) * pBitmap->width) + x];
        memcpy(pDst, pSrc, cx * sizeof(GCCOLOR));
    }
    if (punted)
    {
        GCDelete(&gcPunt);
    }
    if (pulled)
    {
        GCDelete(&gcPull);
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

    if (pBitmap->disposition == GCDeviceMemory)
    {
        GCRECT workarea = {x, y, x+cx, y+cy};
        if (GCCreatePuntSurface(pBitmap->pGC, &workarea, GCFALSE, &gcPunt) == GCFALSE)
            return;
        pBitmap = &gcPunt.bitmap;
        punted = GCTRUE;
    }

    for (dy = 0; dy < cy; dy++)
    {
        GCCOLOR *pDst = &pBitmap->prgBits[((pBitmap->height - 1 - (y + dy)) * pBitmap->width) + x];
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
}

void SWEndAccess(void *hDevice)
{
}

GCBOOL SWMemSetupDevice(GCDEVICE *pDevice, GCBITMAP *pDisplaySurface, void *param)
{
    pDevice->HWBeginAccess        = (HWBEGINACCESS)SWBeginAccess;
    pDevice->HWEndAccess          = (HWENDACCESS)SWEndAccess;
    pDevice->HWGetPixel           = (HWGETPIXEL)SWMemGetPixel;
    pDevice->HWSetPixel           = (HWSETPIXEL)SWMemSetPixel;
    pDevice->HWColorFill          = (HWCOLORFILL)SWMemColorFill;
    pDevice->HWCopyBits           = (HWCOPYBITS)SWMemCopyBits;
    pDevice->HWConstantAlphaBlend = NULL;
    pDevice->HWDimBuffer          = NULL;
    pDevice->hDevice              = NULL;
    pDevice->entryLevel           = 0;

    // Software device gets an empty framebuffer.
    GCInitializeBitmap(pDisplaySurface, 0, 0, 0, GCDeviceMemory);

    return GCTRUE;
}

