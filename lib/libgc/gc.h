#ifdef __cplusplus
extern "C" {
#endif 
typedef struct tagGCFONT
{
    const void *pFont;
    long maxCharWidth;
    long charHeight;
    long charStart;
    long charEnd;
    long encodingType;
} GCFONT, *PGCFONT;

#include "gcconfig.h"

typedef struct tagGCBITMAPFONT
{
    long columns;
    long rows;
    char *alpha;
    GCCOLOR *bits;
} GCBITMAPFONT, *PGCBITMAPFONT;

#ifndef _DEFAULTFONT_
#define GetDefaultFont() NULL
#endif

typedef long GCBOOL;
#define GCTRUE 1
#define GCFALSE 0

typedef struct tagGCDEVICE GCDEVICE;
typedef struct tagGCBITMAP GCBITMAP;
#if 0
typedef unsigned long GCFIXED;

#define GCFIXONE (1 << 16)
#define GCFIXFLOAT(a) ((int)(a* 16))
#define GCUNFIXFLOAT(a) ((int)(a  / 16 ))
#define GCFIX(a) (a << 16)
#define GCUNFIX(a) (a >> 16)
#else
typedef float GCFIXED;

#define GCFIXONE (1)
#define GCFIXFLOAT(a) (a)
#define GCUNFIXFLOAT(a) (a)
#define GCFIX(a) (a)
#define GCUNFIX(a) (a)
#endif

typedef GCBOOL (*HWSETUPDEVICE)(GCDEVICE *pDevice, GCBITMAP *pDisplaySurface, void *param);

typedef GCBOOL (*HWALLOCATESURFACE)(GCBITMAP *pBitmap);

typedef void (*HWFREESURFACE)(GCBITMAP *pBitmap);

typedef GCCOLOR (*HWGETPIXEL)(GCBITMAP *pBitmap,
                              long x,
                              long y);

typedef void (*HWSETPIXEL)(GCBITMAP *pBitmap,
                           long x,
                           long y,
                           GCCOLOR color);

typedef void (*HWCOLORFILL)(GCBITMAP *pBitmap,
                            long x,
                            long y,
                            long cx,
                            long cy,
                            GCCOLOR color);

typedef struct tagGCMATRIXFIXED GCMATRIXFIXED;
typedef struct tagGCMATRIX GCMATRIX;

typedef void (*HWCOPYBITS)(GCBITMAP *pBitmap,
                           long x,
                           long y,
                           long cx,
                           long cy,
                           GCBITMAP *pBitmapSrc,
                           long xSrc,
                           long ySrc,
                           long cxSrc,
                           long cySrc,
                           GCMATRIX *pMatrix);

typedef void (*HWCONSTANTALPHABLEND)(GCBITMAP *pBitmap,
                                     long x,
                                     long y,
                                     long cx,
                                     long cy,
                                     GCBITMAP *pBitmapSrc,
                                     long xSrc,
                                     long ySrc,
                                     long cxSrc,
                                     long cySrc,
                                     long alpha);

typedef void (*HWTRANSPARENTBLT)(GCBITMAP *pBitmap,
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
                                 GCMATRIX *pMatrix);

typedef void (*HWDRAWLINE)(GCBITMAP *pBitmap, long x, long y, long yMajor, double fxdecInc, long incrementVal, GCCOLOR color);

typedef void (*HWBEGINACCESS)(GCBITMAP *pBitmap);
typedef void (*HWENDACCESS)(GCBITMAP *pBitmap);
typedef void (*HWDIMBUFFER)(GCBITMAP *pBitmap);
typedef unsigned long (*HWFRAMEBYTES)(GCBITMAP *pBitmap);
typedef GCBOOL (*HWPRESENTBASE)(GCBITMAP *pBitmap, unsigned long base);

void SWMemSetPixel(GCBITMAP *pBitmap, long x, long y, GCCOLOR color);
GCCOLOR SWMemGetPixel(GCBITMAP *pBitmap, long x, long y);
void SWMemCopyBits(GCBITMAP *pBitmap, long x, long y, long cx, long cy, GCBITMAP *pBitmapSource, long xSrc, long ySrc, long cxSrc, long cySrc, GCMATRIX *pMatrix);
void SWMemColorFill(GCBITMAP *pBitmap, long x, long y, long cx, long cy, GCCOLOR color);
void SWBeginAccess(void *hDevice);
void SWEndAccess(void *hDevice);

typedef struct tagGC GC;

typedef struct tagGCMATRIXFIXED
{
    GCFIXED m11;
    GCFIXED m21;
    GCFIXED m31;
    GCFIXED m12;
    GCFIXED m22;
    GCFIXED m32;
    GCFIXED OffsetX;
    GCFIXED OffsetY;
    GCFIXED OffsetZ;
} GCMATRIXFIXED;

typedef struct tagGCMATRIX
{
	union
	{
		GCMATRIXFIXED fixed;
		GCFIXED array[3][3];
	};
} GCMATRIX;

typedef struct tagGCRECT
{
    long left;
    long top;
    long right;
    long bottom;
} GCRECT;

typedef struct tagGCPOINT
{
    long x;
    long y;
} GCPOINT;

typedef struct tagGCDEVICE {
    GC *pDisplay;
    HWBEGINACCESS HWBeginAccess;
    HWENDACCESS HWEndAccess;
    HWGETPIXEL HWGetPixel;
    HWSETPIXEL HWSetPixel;
    HWCOLORFILL HWColorFill;
    HWCOPYBITS HWCopyBits;
    HWCONSTANTALPHABLEND HWConstantAlphaBlend;
    HWTRANSPARENTBLT HWTransparentBlt;
    HWDIMBUFFER HWDimBuffer;
    HWDRAWLINE HWDrawLine;
    HWFRAMEBYTES HWFrameBytes;
    HWPRESENTBASE HWPresentBase;
    void* hDevice;
    unsigned long entryLevel;
} GCDEVICE, *PGCDEVICE;

typedef enum tagGCBITMAPDISPOSITION {
    GCSystemMemoryManaged,
    GCSystemMemoryPreAllocated,
    GCSystemMemoryPuntSurface,
    GCDeviceMemory,
    GCDeviceDisplayOnly
} GCBITMAPDISPOSITION;

typedef struct tagGCBITMAP {
    GCBITMAPDISPOSITION disposition;
    long width;
    long height;
    unsigned long deviceBase;
    union {
        unsigned long handle;
        GCCOLOR *prgBits;
    };
    GC *pGC;
    GC *pGCPalette;
    GCDEVICE *pDevice;
    unsigned long entryLevel;
} GCBITMAP, *PGCBITMAP;

typedef struct tagGCBLITEFFECT
{
    long EffectType;
    GCCOLOR param;
} GCBLITEFFECT;

typedef struct tagGC
{
    unsigned long flags;
    GCDEVICE device;
    GCBITMAP bitmap;
    GCCOLOR backgroundColor;
    GCCOLOR foregroundColor;
    long fillMode;
    GCFONT *pFont;
    GCCOLOR *prgPalette;
    char paletteSize;
    char paletteOffset;
    GCCOLOR gradientStart;
    GCCOLOR gradientFinish;
    long gradientFillMode;
    long colorMask;
    long textMode;
    long offsetX;
    long offsetY;
    GCBLITEFFECT blitEffect;
    struct tagGC *pGCCacheGradient;
    struct tagGC *pPuntGC;
    GCRECT PuntWorkArea;
} GC, *PGC;

#define GCF_DISABLEACCELERATION 0x00000001

#ifndef MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#endif
#define RECTWIDTH(rc) (rc->right - rc->left ? rc->right - rc->left : 1)
#define RECTHEIGHT(rc) (rc->bottom - rc->top ? rc->bottom - rc->top : 1)
#define SETRECT(rc, a, b, c, d) {rc->left = a; rc->top = b; rc->right = c; rc->bottom = d;}
#define OFFSETRECT(rc, a, b) {rc->left +=a; rc->right += a; rc->top += b; rc->bottom += b;}
#define EMPTYRECT(rc) (rc->right <= rc->left || rc->bottom <= rc->top)
#define WITHINRECT(rc, x, y) (rc->left <= x && rc->right >= x && rc->top <= y && rc->bottom >= y)

#define GCBLIT_NORMAL        0
#define GCBLIT_COLORKEY      1
#define GCBLIT_CONSTANTALPHA 2
#define GCBLIT_PERPIXELALPHA 3

#define GCTRANSPARENT   0x0
#define GCOPAQUE        0x1
#define GCGRADIENT      0x2
#define GCTRANSPARENTFG 0x0
#define GCOPAQUEFG      0x10
#define GCGRADIENTFG    0x20

#define GRADIENTMODES(g) ((g & GCGRADIENT || g & GCGRADIENTFG))

#define GCHGRADIENT 0
#define GCVGRADIENT 1

#define GCTEXT_WRAP 0
#define GCTEXT_SINGLELINE 1

GCBOOL GCCreateWithDeviceMemory(GC *pGCSource, long width, long height, GC *pGC);
GCBOOL GCCreateWithMatchingMemoryLocale(GC *pGCSource, long width, long height, GC *pGC);
GCBOOL GCCreateWithPreallocatedMemory(GC *pGCSource, long width, long height, GCCOLOR *prgBits, GC *pGC);
GCBOOL GCCreateWithSystemMemory(GC *pGCSource, long width, long height, GC *pGC);
GCBOOL GCInitialize(HWSETUPDEVICE callback, void *param, GC *pGC);
void GCInitializeBitmap(GCBITMAP *pGCBitmap, long width, long height, unsigned long handle, GCBITMAPDISPOSITION disposition);
GCBOOL GCCreatePuntSurface(GC *pGCSource, GCRECT *pWorkArea, GCBOOL initialize, GC *pGCPuntSurface);
void GCDelete(GC *pGC);

void GCEnableAcceleration(GC *pGC, GCBOOL disable);

GCCOLOR GCColorFrom888(unsigned long color);
unsigned long GCColorTo888(GCCOLOR color);

long GCWidth(GC *pGC);
long GCHeight(GC *pGC);
PGC GCDisplay(void);
unsigned long GCDeviceFrameBytes(GC *pGC);
unsigned long GCDeviceBase(GC *pGC);
void GCSetDeviceBase(GC *pGC, unsigned long deviceBase);
GCBOOL GCPresentDeviceBase(GC *pGC, unsigned long base);
GCBOOL GCPresentDeviceDrawBase(GC *pGC);

void GCSetBackgroundColor(GC *pGC, GCCOLOR color);
void GCSetForegroundColor(GC *pGC, GCCOLOR color);
void GCSetFillMode(GC *pGC, long fillmode);
void GCSetTextMode(GC *pGC, long textmode);
void GCSetOffset(GC *pGC, long offsetX, long offsetY);
void GCSetGradientParameters(GC *pGC, GCCOLOR color1, GCCOLOR color2, long fillmode);
void GCSetColorMask(GC *pGC, long mask);
void GCSetHWPalette(GC *pGC, GC* pGCPalette);
void GCSetPalette(GC *pGC, GCCOLOR *prgPalette, char palettesize);

void GCPBeginAccess(GC *pGC);
void GCPEndAccess(GC *pGC);

void GCSetBlitEffect(GC *pGC, long pixelEffect, GCCOLOR param);

void GCCopyBits(GC *pGC, GC *pGCSrc);
void GCCopyBitsXY(GC *pGC, GC *pGCSrc, long xDst, long yDst);
void GCCopyBits2(GC *pGC, GC *pGCSrc, GCRECT *prcSrc, GCPOINT *pptDst);
void GCStretchBits(
  GC 	*pGC,
  int   nXOriginDest,
  int   nYOriginDest,
  int   nWidthDest,
  int   nHeightDest,
  GC 	*pGCSrc,
  int   nXOriginSrc,
  int   nYOriginSrc,
  int   nWidthSrc,
  int   nHeightSrc
  );
void GCStretchBits2(
  GC *pGCDst,
  GCRECT *prcDst,
  GC *pGCSrc,
  GCRECT *prcSrc
  );
void GCMatrixBlt(
  GC 	*pGC,
  int   nXOriginDest,
  int   nYOriginDest,
  GC 	*pGCSrc,
  int   nXOriginSrc,
  int   nYOriginSrc,
  int   nWidthSrc,
  int   nHeightSrc,
  GCMATRIX *pMatrix
  );

void GCBurnPixel(GC *pGC, long x, long y, GCCOLOR color);
void GCPMemSetPixel(GC *pGC, long x, long y, GCCOLOR color);
GCCOLOR GCPMemGetPixel(GC *pGC, long x, long y);
void GCPMemCopyBits(GC *pGC, long x, long y, long cx, long cy, GCCOLOR *pulBits, long xSrc, long ySrc, long cxSrc, long cySrc);
void GCPMemColorFill(GC *pGC, long x, long y, long cx, long cy, GCCOLOR color);
void GCSetPixel(GC *pGC, long x, long y, GCCOLOR color);
GCCOLOR GCGetPixel(GC *pGC, long x, long y);
GCCOLOR GCGetPixelFromPalette(GC *pGC, long x, long y);

void GCDimBuffer(GC *pGC);

void GCRotatePalette(GC *pGC);

void GCFloodFill(GC *pGC, long x, long y, GCCOLOR color);

void GCFastFill(GC *pGC, long x, long y, long width, long height, GCCOLOR color);
void GCDrawRectangle(GC *pGC, long x, long y, long width, long height);

void GCDrawHGradientRectangle(GC *pGC, long x, long y, long width, long height, GCCOLOR color1, GCCOLOR color2);
void GCDrawVGradientRectangle(GC *pGC, long x, long y, long width, long height, GCCOLOR color1, GCCOLOR color2);
void GCBeginCacheGradient(GC *pGCSource, GC *pGC, long width, long height);
void GCEndCacheGradient(GC *pGC);

void GCDrawCircle(GC *pGC, long x, long y, long radius);
void GCDrawLine(GC *pGC, long x, long y, long x2, long y2);

void GCSetFont(GC *pGC, GCFONT *pFont);
void GCDrawText(GC *pGC, char text[], long startx, long starty, long endx);

void GCRotateSurface(GC *pGC, long degrees);

GCMATRIX *GCMxIdentity();

void GCMxInitialize(
    GCMATRIX *pMatrix
    );

void GCMxRotate(
    GCMATRIX *pMatrix,
    float angle
    );

void GCMxTranslate(
    GCMATRIX *pMatrix,
    float x,
    float y
    );

void GCMxScale(
    GCMATRIX *pMatrix,
    float scalex,
    float scaley
    );

void GCMxInverse(
	GCMATRIX *pSrc,
	GCMATRIX *pDst
	);

void GCMxMultiply(
    GCMATRIX *result,
    GCMATRIX *a,
    GCMATRIX *b
    );

void GCMxTransformPoint(
    GCPOINT *pPoint,
    GCMATRIX *pTransform
    );

void GCMxGetPolyBounds(
	GC *pGC,
	GCRECT *pBounds,
	GCPOINT *pPoly,
	unsigned long polys,
	GCMATRIX *pMatrix,
	GCPOINT *pOrigin
	);
#ifdef __cplusplus
}
#endif 
