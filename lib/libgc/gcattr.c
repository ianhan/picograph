#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gc.h"

long GCWidth(GC *pGC)
{
    return pGC->bitmap.width;
}

long GCHeight(GC *pGC)
{
    return pGC->bitmap.height;
}

unsigned long GCDeviceFrameBytes(GC *pGC)
{
    if (!pGC || !pGC->device.HWFrameBytes) {
        return 0;
    }
    return pGC->device.HWFrameBytes(pGC->device.hDevice);
}

unsigned long GCDeviceBase(GC *pGC)
{
    return pGC->bitmap.deviceBase;
}

void GCSetDeviceBase(GC *pGC, unsigned long deviceBase)
{
    pGC->bitmap.deviceBase = deviceBase;
}

GCBOOL GCPresentDeviceBase(GC *pGC, unsigned long base)
{
    GCBOOL result;

    if (!pGC || !pGC->device.HWPresentBase) {
        return GCFALSE;
    }

    GCPBeginAccess(pGC);
    result = pGC->device.HWPresentBase(pGC->device.hDevice, base);
    GCPEndAccess(pGC);
    return result;
}

GCBOOL GCPresentDeviceDrawBase(GC *pGC)
{
    if (!pGC) {
        return GCFALSE;
    }
    return GCPresentDeviceBase(pGC, GCDeviceBase(pGC));
}

void GCSetBackgroundColor(GC *pGC, GCCOLOR color)
{
    pGC->backgroundColor = color;
}

void GCSetForegroundColor(GC *pGC, GCCOLOR color)
{
    pGC->foregroundColor = color;
}

void GCSetFillMode(GC *pGC, long fillmode)
{
    pGC->fillMode = fillmode;
}

void GCSetTextMode(GC *pGC, long textmode)
{
    pGC->textMode = textmode;
}

void GCSetOffset(GC *pGC, long offsetX, long offsetY)
{
    pGC->offsetX = offsetX;
    pGC->offsetY = offsetY;
}

void GCSetPalette(GC *pGC, GCCOLOR *prgPalette, char palettesize)
{
    pGC->prgPalette = prgPalette;
    pGC->paletteSize = palettesize;
}

void GCSetHWPalette(GC *pGC, GC* pGCPalette)
{
	pGC->bitmap.pGCPalette = pGCPalette;
}

void GCRotatePalette(GC *pGC)
{
    pGC->paletteOffset++;
}

void GCSetGradientParameters(GC *pGC, GCCOLOR color1, GCCOLOR color2, long fillmode)
{
    pGC->gradientStart = color1;
    pGC->gradientFinish = color2;
    pGC->gradientFillMode = fillmode;
}

void GCSetColorMask(GC *pGC, long mask)
{
    pGC->colorMask = mask;
}

void GCEnableAcceleration(GC *pGC, GCBOOL enable)
{
    if (enable == GCTRUE)
        pGC->flags &= ~GCF_DISABLEACCELERATION;
    else
        pGC->flags |= GCF_DISABLEACCELERATION;
}

void GCSetBlitEffect(GC *pGC, long blitEffect, GCCOLOR param)
{
    pGC->blitEffect.EffectType = blitEffect;
    pGC->blitEffect.param = param;
}

void GCSetFont(GC *pGC, GCFONT *pFont)
{
    pGC->pFont = pFont;
}


