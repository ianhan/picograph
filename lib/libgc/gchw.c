#include <stdio.h>
#include <system.h>
#include "gc.h"

#define UNREFERENCED_PARAMETER(n) (n)
#define GPU_RESET 0
#define GPU_EXEC 0x800
#define GRAPHICS_PIPE_BASE 0x08008000
#define GPUREBASE(n) (GRAPHICS_PIPE_BASE + ((n) << 2))
#define GPUIOREBASE(n) (GRAPHICS_PIPE_BASE + ((n) << 2))

#define WriteRegister(address, value) *((unsigned long*)address) = value;

void WriteFloatToFixedRegister(unsigned long address, float value)
{
	unsigned long value2 = (unsigned long) (float)(fabs(value) * (1<<16));
	if (value < 0.0f)
		value2 |= 0x80000000;
	WriteRegister(address, value2);
}

typedef enum {
    REG_ROP,
    REG_COMPOSITEMODE,
    REG_SOURCEADDRESS,
    REG_SOURCESTRIDE,
    REG_SOURCEX,
    REG_SOURCEY,
    REG_SOURCEBOUNDSX,
    REG_SOURCEBOUNDSY,
    REG_DESTADDRESS,
    REG_DESTSTRIDE,
    REG_DESTX,
    REG_DESTY,
    REG_DESTWIDTH,
    REG_DESTHEIGHT,
    REG_COLOR,
    REG_PALETTE,
    REG_EXTRADATA,
    REG_M11,
    REG_M12,
    REG_M21,
    REG_M22,
    REG_M31,
    REG_M32,
    REG_CLIPPEDDESTX,
    REG_CLIPPEDDESTY,
    REG_COUNT=32
} REGISTER;

typedef enum {
    ROP_NOP = 0,
    ROP_COLORFILL = 1,
    ROP_BLT = 2,
    ROP_GRADIENT = 4,
    ROP_DECAY = 8,
    ROP_DOWNLOAD = 16
} COMMANDROP;

typedef enum {
    ROP_OVER = 0,
    ROP_COLORKEY = 1,
    ROP_ALPHA = 2,
    ROP_BURN = 4,
    ROP_INVERT = 8,
    ROP_OPACITY = 16,
    ROP_8BIT = 32,
    ROP_PALETTE = 64,
    ROP_BRESENHAM = 128,
    ROP_XFORM = 256
} FLAGROP;

void HWBeginAccess(void *hDevice)
{
}

void HWEndAccess(void *hDevice)
{
	alt_busy_sleep(10000);
}

void HWColorFill(GCBITMAP *pBitmap, long x, long y, long cx, long cy, GCCOLOR color)
{
	WriteRegister(GPUREBASE(REG_ROP), ROP_COLORFILL);
	WriteRegister(GPUREBASE(REG_DESTADDRESS), pBitmap->handle);
	WriteRegister(GPUREBASE(REG_DESTSTRIDE), pBitmap->width);
	WriteRegister(GPUREBASE(REG_DESTX), x);
	WriteRegister(GPUREBASE(REG_DESTY), y);
	WriteRegister(GPUREBASE(REG_DESTWIDTH), cx);
	WriteRegister(GPUREBASE(REG_DESTHEIGHT), cy);
	WriteRegister(GPUREBASE(REG_COLOR), color);
	WriteRegister(GPUIOREBASE(GPU_EXEC), 1);
}

void HWCopyBits(GCBITMAP *pBitmap,
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

	int clippeddestx = 0;
	int clippeddesty = 0;
    if (x < 0)
    {
    	clippeddestx = abs(x);
    	x = 0;
    	cx -= clippeddestx;
    }
    if (y < 0)
    {
    	clippeddesty = abs(y);
    	y = 0;
    	cy -= clippeddesty;
    }

    WriteRegister(GPUREBASE(REG_ROP), ROP_BLT);
    WriteRegister(GPUREBASE(REG_DESTADDRESS), pBitmap->handle);
    WriteRegister(GPUREBASE(REG_DESTSTRIDE), pBitmap->width);
    WriteRegister(GPUREBASE(REG_DESTX), x);
    WriteRegister(GPUREBASE(REG_DESTY), y);
    WriteRegister(GPUREBASE(REG_DESTWIDTH), cx);
    WriteRegister(GPUREBASE(REG_DESTHEIGHT), cy);
    WriteRegister(GPUREBASE(REG_SOURCEADDRESS), pBitmapSrc->handle);
    WriteRegister(GPUREBASE(REG_SOURCESTRIDE), pBitmapSrc->width);
    WriteRegister(GPUREBASE(REG_SOURCEX), xSrc);
    WriteRegister(GPUREBASE(REG_SOURCEY), ySrc);
    WriteRegister(GPUREBASE(REG_SOURCEBOUNDSX), pBitmapSrc->width);
    WriteRegister(GPUREBASE(REG_SOURCEBOUNDSY), pBitmapSrc->height);
    WriteRegister(GPUREBASE(REG_CLIPPEDDESTX), clippeddestx);
    WriteRegister(GPUREBASE(REG_CLIPPEDDESTY), clippeddesty);
    if (pMatrix)
    {
        WriteRegister(GPUREBASE(REG_COMPOSITEMODE), ROP_XFORM);
        WriteFloatToFixedRegister(GPUREBASE(REG_M11), pMatrix->array[0][0]);
        WriteFloatToFixedRegister(GPUREBASE(REG_M12), pMatrix->array[0][1]);
        WriteFloatToFixedRegister(GPUREBASE(REG_M21), pMatrix->array[1][0]);
		WriteFloatToFixedRegister(GPUREBASE(REG_M22), pMatrix->array[1][1]);
		WriteFloatToFixedRegister(GPUREBASE(REG_M31), pMatrix->array[2][0]);
		WriteFloatToFixedRegister(GPUREBASE(REG_M32), pMatrix->array[2][1]);
    }
    else if (cx != cxSrc || cy != cySrc)
    {
        WriteRegister(GPUREBASE(REG_COMPOSITEMODE), ROP_XFORM);
        int x_ratio = (int)((cxSrc<<16)/cx) + 1;
        int y_ratio = (int)((cySrc<<16)/cy) + 1;
        WriteRegister(GPUREBASE(REG_M11), x_ratio);
        WriteRegister(GPUREBASE(REG_M22), y_ratio);
    }
	else
	{
		WriteRegister(GPUREBASE(REG_COMPOSITEMODE), 0);
	}

    if (pBitmapSrc->pGCPalette)
    {
        static unsigned char i = 0;
        WriteRegister(GPUREBASE(REG_PALETTE), pBitmapSrc->pGCPalette->bitmap.handle);
        WriteRegister(GPUREBASE(REG_EXTRADATA), i);
        WriteRegister(GPUREBASE(REG_COMPOSITEMODE), ROP_8BIT | (pMatrix ? ROP_XFORM : 0));
        //i++;
    }

    WriteRegister(GPUIOREBASE(GPU_EXEC), 1);
    //WriteRegister(GPUREBASE(REG_COMPOSITEMODE), 0);
}

void HWTransparentBlt(GCBITMAP *pBitmap,
                      long x,
                      long y,
                      long cx,
                      long cy,
                      GCBITMAP *pBitmapSrc,
                      long xSrc,
                      long ySrc,
                      long cxSrc,
                      long cySrc,
                      GCCOLOR colorkey,
                      GCMATRIX *pMatrix)
{
	int clippeddestx = 0;
	int clippeddesty = 0;
    if (x < 0)
    {
    	clippeddestx = abs(x);
    	x = 0;
    	cx -= clippeddestx;
    }
    if (y < 0)
    {
    	clippeddesty = abs(y);
    	y = 0;
    	cy -= clippeddesty;
    }

    WriteRegister(GPUREBASE(REG_ROP), ROP_BLT);
    WriteRegister(GPUREBASE(REG_DESTADDRESS), pBitmap->handle);
    WriteRegister(GPUREBASE(REG_DESTSTRIDE), pBitmap->width);
    WriteRegister(GPUREBASE(REG_DESTX), x);
    WriteRegister(GPUREBASE(REG_DESTY), y);
    WriteRegister(GPUREBASE(REG_DESTWIDTH), cx);
    WriteRegister(GPUREBASE(REG_DESTHEIGHT), cy);
    WriteRegister(GPUREBASE(REG_SOURCEADDRESS), pBitmapSrc->handle);
    WriteRegister(GPUREBASE(REG_SOURCESTRIDE), pBitmapSrc->width);
    WriteRegister(GPUREBASE(REG_SOURCEX), xSrc);
    WriteRegister(GPUREBASE(REG_SOURCEY), ySrc);
    WriteRegister(GPUREBASE(REG_SOURCEBOUNDSX), pBitmapSrc->width);
    WriteRegister(GPUREBASE(REG_SOURCEBOUNDSY), pBitmapSrc->height);
    WriteRegister(GPUREBASE(REG_CLIPPEDDESTX), clippeddestx);
    WriteRegister(GPUREBASE(REG_CLIPPEDDESTY), clippeddesty);
    if (pMatrix)
    {
        WriteRegister(GPUREBASE(REG_COMPOSITEMODE), ROP_COLORKEY|ROP_XFORM);
        WriteFloatToFixedRegister(GPUREBASE(REG_M11), pMatrix->array[0][0]);
        WriteFloatToFixedRegister(GPUREBASE(REG_M12), pMatrix->array[0][1]);
        WriteFloatToFixedRegister(GPUREBASE(REG_M21), pMatrix->array[1][0]);
		WriteFloatToFixedRegister(GPUREBASE(REG_M22), pMatrix->array[1][1]);
		WriteFloatToFixedRegister(GPUREBASE(REG_M31), pMatrix->array[2][0]);
		WriteFloatToFixedRegister(GPUREBASE(REG_M32), pMatrix->array[2][1]);
    }
    else if (cx != cxSrc || cy != cySrc)
    {
        WriteRegister(GPUREBASE(REG_COMPOSITEMODE), ROP_COLORKEY|ROP_XFORM);
        int x_ratio = (int)((cxSrc<<16)/cx) + 1;
        int y_ratio = (int)((cySrc<<16)/cy) + 1;
        WriteRegister(GPUREBASE(REG_M11), x_ratio);
        WriteRegister(GPUREBASE(REG_M22), y_ratio);
    }
	else
	{
		WriteRegister(GPUREBASE(REG_COMPOSITEMODE), ROP_COLORKEY);
	}
    WriteRegister(GPUREBASE(REG_COLOR), colorkey);
    WriteRegister(GPUIOREBASE(GPU_EXEC), 1);
}

void HWDimBuffer(GCBITMAP *pBitmap)
{
    WriteRegister(GPUREBASE(REG_ROP), ROP_DECAY);
    WriteRegister(GPUREBASE(REG_DESTADDRESS), pBitmap->handle);
    WriteRegister(GPUREBASE(REG_DESTSTRIDE), pBitmap->width);
    WriteRegister(GPUREBASE(REG_DESTX), 0);
    WriteRegister(GPUREBASE(REG_DESTY), 0);
    WriteRegister(GPUREBASE(REG_DESTWIDTH), pBitmap->width);
    WriteRegister(GPUREBASE(REG_DESTHEIGHT), pBitmap->height);
    WriteRegister(GPUREBASE(REG_EXTRADATA), 8);
    WriteRegister(GPUIOREBASE(GPU_EXEC), 1);
}

void HWDrawLine(GCBITMAP *pBitmap,
				long x,
				long y,
				long yMajor,
				double fxdecInc,
				long incrementVal,
				GCCOLOR color)
{
}

void GCHWAccelerateContext(GC *pGC)
{
    pGC->device.HWBeginAccess = (HWBEGINACCESS) HWBeginAccess;
    pGC->device.HWEndAccess = (HWENDACCESS) HWEndAccess;
    //pGC->device.HWColorFill =  (HWCOLORFILL)HWColorFill;
    pGC->device.HWCopyBits = (HWCOPYBITS) HWCopyBits;
    pGC->device.HWTransparentBlt = (HWTRANSPARENTBLT)HWTransparentBlt;
    pGC->device.HWConstantAlphaBlend = (HWCONSTANTALPHABLEND)NULL;// HWConstantAlphaBlend;
    pGC->device.HWDimBuffer = (HWDIMBUFFER)HWDimBuffer;
    pGC->device.HWDrawLine = (HWDIMBUFFER)NULL;//HWDrawLine;
    pGC->device.HWFrameBytes = (HWFRAMEBYTES)NULL;
    pGC->device.HWPresentBase = (HWPRESENTBASE)NULL;
}
