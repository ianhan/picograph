#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define _GC_
#include "gc.h"

void GCPSetDefaults(GC *pGC, GC *pGCSource)
{
    pGC->flags = 0;
    pGC->backgroundColor = 0;
    pGC->foregroundColor = (RMASK | GMASK | BMASK | AMASK);
    pGC->fillMode = (GCOPAQUE | GCOPAQUEFG);
    pGC->prgPalette = NULL;
    pGC->paletteSize = 0;
    pGC->paletteOffset = 0;
    pGC->pFont = GetDefaultFont();
    pGC->device.entryLevel = 0;
    pGC->colorMask = (RMASK | GMASK | BMASK | AMASK);
    pGC->textMode = GCTEXT_WRAP;
    pGC->offsetX = 0;
    pGC->offsetY = 0;
    pGC->blitEffect.EffectType = GCBLIT_NORMAL;
    pGC->blitEffect.param = 0;
    pGC->pGCCacheGradient = NULL;
    pGC->PuntWorkArea.left = pGC->PuntWorkArea.top = pGC->PuntWorkArea.right  = pGC->PuntWorkArea.bottom = 0;
        
    if (pGCSource)
    {
        pGC->device = pGCSource->device;
    }
}

GCBOOL GCInitialize(HWSETUPDEVICE HWCBSetupDevice, void *param, GC *pGC)
{
    GCPSetDefaults(pGC, NULL);
    pGC->device.pDisplay = pGC;
    return HWCBSetupDevice(&pGC->device, &pGC->bitmap, param);
}

void GCInitializeBitmap(GCBITMAP *pGCBitmap, long width, long height, unsigned long handle, GCBITMAPDISPOSITION disposition)
{
    pGCBitmap->disposition = disposition;
    pGCBitmap->handle = handle;
    pGCBitmap->width = width;
    pGCBitmap->height = height;
	pGCBitmap->pDevice = &pGCBitmap->pGC->device;

	if (disposition != GCDeviceDisplayOnly &&
    	disposition != GCDeviceMemory)
    {
		pGCBitmap->pDevice->hDevice = pGCBitmap;
		if (disposition == GCSystemMemoryManaged)
		{
			GCFastFill(pGCBitmap->pGC, 0, 0, width, height, 0);
		}
    }
}

GCBOOL GCCreateWithSystemMemory(GC *pGCSource, long width, long height, GC *pGC)
{
    GCCOLOR *prgBits = (GCCOLOR*)malloc(width*height*sizeof(GCCOLOR));

    if (prgBits == NULL)
        return GCFALSE;

    GCPSetDefaults(pGC, pGCSource);
    pGC->device.HWBeginAccess        = (HWBEGINACCESS)SWBeginAccess;
    pGC->device.HWEndAccess          = (HWENDACCESS)SWEndAccess;
    pGC->device.HWGetPixel           = (HWGETPIXEL)SWMemGetPixel;
    pGC->device.HWSetPixel           = (HWSETPIXEL)SWMemSetPixel;
    pGC->device.HWColorFill          = (HWCOLORFILL)SWMemColorFill;
    pGC->device.HWCopyBits           = (HWCOPYBITS)SWMemCopyBits;
    pGC->bitmap.pGC = pGC;
    GCInitializeBitmap(&pGC->bitmap, width, height, (unsigned long) prgBits, GCSystemMemoryManaged);
    return GCTRUE;
}

GCBOOL GCCreateWithPreallocatedMemory(GC *pGCSource, long width, long height, GCCOLOR *prgBits, GC *pGC)
{
    GCPSetDefaults(pGC, pGCSource);
    pGC->device.HWBeginAccess        = (HWBEGINACCESS)SWBeginAccess;
    pGC->device.HWEndAccess          = (HWENDACCESS)SWEndAccess;
    pGC->device.HWGetPixel           = (HWGETPIXEL)SWMemGetPixel;
    pGC->device.HWSetPixel           = (HWSETPIXEL)SWMemSetPixel;
    pGC->device.HWColorFill          = (HWCOLORFILL)SWMemColorFill;
    pGC->device.HWCopyBits           = (HWCOPYBITS)SWMemCopyBits;
    pGC->bitmap.pGC = pGC;
    GCInitializeBitmap(&pGC->bitmap, width, height, (unsigned long) prgBits, GCSystemMemoryPreAllocated);
    return GCTRUE;
}

GCBOOL GCCreateWithDeviceMemory(GC *pGCSource, long width, long height, GC *pGC)
{
    GCPSetDefaults(pGC, pGCSource);
    
    return GCTRUE;
}

GCBOOL GCCreateWithMatchingMemoryLocale(GC *pGCSource, long width, long height, GC *pGC)
{
    return pGCSource->bitmap.disposition == GCDeviceMemory ? 
        GCCreateWithDeviceMemory(pGCSource, width, height, pGC) :
        GCCreateWithSystemMemory(pGCSource, width, height, pGC);
}

GCBOOL GCCreatePuntSurface(GC *pGCSource, GCRECT *pWorkArea, GCBOOL initialize, GC *pGCPuntSurface)
{
    GCBOOL gcRet = GCFALSE;

    if (GCTRUE == (gcRet = GCCreateWithSystemMemory(pGCSource, RECTWIDTH(pWorkArea), RECTHEIGHT(pWorkArea), pGCPuntSurface)))
    {
        GCPOINT point = {0, 0};
        if (initialize)
        {
            GCCopyBits2(pGCPuntSurface, pGCSource, pWorkArea, &point);
        }
        pGCPuntSurface->PuntWorkArea = *pWorkArea;
        pGCPuntSurface->bitmap.disposition = GCSystemMemoryPuntSurface;
    }

    return gcRet;
}

void GCDelete(GC *pGC)
{
    //  TODO: device free
    if (pGC->bitmap.disposition == GCSystemMemoryPuntSurface)
    {
        GCPOINT point = {pGC->PuntWorkArea.left, pGC->PuntWorkArea.top};
        GCRECT dest = pGC->PuntWorkArea;
        OFFSETRECT((&dest), point.x, point.y);
        GCCopyBits2(pGC, pGC->pPuntGC, &dest, &point);
        pGC->bitmap.disposition = GCSystemMemoryManaged;
    }
    if (pGC->bitmap.disposition == GCSystemMemoryManaged)
    {
        free(pGC->bitmap.prgBits);
    }
}
