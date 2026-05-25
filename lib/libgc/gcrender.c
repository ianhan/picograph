#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gc.h"
#include <ctype.h>

void GCPEndGradient(GC *pGC);
GCCOLOR GCPSourceGradient(GC *pGC, long x, long y);
void GCPBeginGradient(GC *pGCSource, GC *pGC, long width, long height);

GCBOOL GCPAccelerate(GC *pGC)
{
    return !(pGC->flags & GCF_DISABLEACCELERATION);
}

void GCPBeginAccess(GC *pGC)
{
    if (pGC->device.entryLevel == 0)
    {
        if (pGC->device.HWBeginAccess)
        {
            pGC->device.HWBeginAccess(pGC->device.hDevice);
        }
    }
    pGC->device.entryLevel++;
}

void GCPEndAccess(GC *pGC)
{
    pGC->device.entryLevel--;
    if (pGC->device.entryLevel == 0)
    {
        if (pGC->device.HWEndAccess)
        {
            pGC->device.HWEndAccess(pGC->device.hDevice);
        }
    }
}

GCCOLOR GCGetPixelFromPalette(GC *pGC, long x, long y)
{
    return (pGC->prgPalette ? pGC->prgPalette[(GCGetPixel(pGC, x, y) + pGC->paletteOffset) % pGC->paletteSize] : GCGetPixel(pGC, x, y));
}

void GCPFloodFill(GC *pGC, long x, long y, GCCOLOR color, GCCOLOR replace)
{
    if (GCGetPixel(pGC, x, y) == replace)
    {
        GCSetPixel(pGC, x, y, color);
        GCPFloodFill(pGC, x, y+1, color, replace);
        GCPFloodFill(pGC, x, y-1, color, replace);
        GCPFloodFill(pGC, x+1, y, color, replace);
        GCPFloodFill(pGC, x-1, y, color, replace);
    }
}

void GCFloodFill(GC *pGC, long x, long y, GCCOLOR color)
{
    GCPBeginAccess(pGC);
    GCPFloodFill(pGC, x, y, color, GCGetPixel(pGC, x, y));
    GCPEndAccess(pGC);
}

void GCFastFill(GC *pGC, long x, long y, long width, long height, GCCOLOR color)
{
    GCPBeginAccess(pGC);
    if (GCPAccelerate(pGC) && pGC->device.HWColorFill != NULL)
    {
        pGC->device.HWColorFill(pGC->device.hDevice, x + pGC->offsetX, y + pGC->offsetY, width, height, color);
    }
    else
    {
        long i;
        GCCOLOR oldForeground = pGC->foregroundColor;
        long oldfillMode = pGC->fillMode;
        pGC->fillMode = (GCOPAQUEFG | GCTRANSPARENT);
        pGC->foregroundColor = color;
        for (i = 0; i < height; i++)
        {
            GCDrawLine(pGC, x, y+i, x+width, y+i);
        }
        pGC->foregroundColor = oldForeground;
        pGC->fillMode = oldfillMode;
    }
    GCPEndAccess(pGC);
}

void GCDrawRectangle(GC *pGC, long x, long y, long width, long height)
{
    if (!width || !height)
        return;

    GCPBeginAccess(pGC);

    if (pGC->fillMode & GCOPAQUEFG)
    {
        GCFastFill(pGC, pGC->offsetX + x, pGC->offsetY + y, 1, height, pGC->foregroundColor);
        GCFastFill(pGC, pGC->offsetX + x+width-1, pGC->offsetY + y, 1, height, pGC->foregroundColor);
        GCFastFill(pGC, pGC->offsetX + x+1, pGC->offsetY + y, width-2, 1, pGC->foregroundColor);
        GCFastFill(pGC, pGC->offsetX + x+1, pGC->offsetY + y+height-1, width-2, 1, pGC->foregroundColor);
    }
    else if (pGC->fillMode & GCGRADIENTFG)
    {
        switch(pGC->gradientFillMode)
        {
        default:
        case GCHGRADIENT:
            GCDrawHGradientRectangle(pGC, x, y, width, height, pGC->gradientStart, pGC->gradientFinish);
            break;
        case GCVGRADIENT:
            GCDrawVGradientRectangle(pGC, x, y, width, height, pGC->gradientStart, pGC->gradientFinish);
            break;
        }
    }
    // Do the fill, if needed
    if (pGC->fillMode & GCOPAQUE)
    {
        if (width > 2 && height > 2)
        {
            GCFastFill(pGC, x+1, y+1, width-2, height-2, pGC->backgroundColor);
        }
    }
    else if (pGC->fillMode & GCGRADIENT)
    {
        if (width > 2 && height > 2)
        {
            switch(pGC->gradientFillMode)
            {
            default:
            case GCHGRADIENT:
                GCDrawHGradientRectangle(pGC, x+1, y+1, width-2, height-2, pGC->gradientStart, pGC->gradientFinish);
                break;
            case GCVGRADIENT:
                GCDrawVGradientRectangle(pGC, x+1, y+1, width-2, height-2, pGC->gradientStart, pGC->gradientFinish);
                break;
            }
        }

    }

    GCPEndAccess(pGC);
}

void GCDrawHGradientRectangle(GC *pGC, long x, long y, long width, long height, GCCOLOR color1, GCCOLOR color2)
{
    int nAlpha1, nAlpha2, nAlpha;
    int nRed1, nRed2, nRed;
    int nGreen1, nGreen2, nGreen;
    int nBlue1, nBlue2, nBlue;
    float dRed, dGreen, dBlue, dAlpha;
    long dy, dx;
    GC gcScan;

    if (!width || !height)
        return;

    GCPBeginAccess(pGC);

    nRed1 = GETR(color1);
    nRed2 = GETR(color2);
    nGreen1 = GETG(color1);
    nGreen2 = GETG(color2);
    nBlue1 = GETB(color1);
    nBlue2 = GETB(color2);
    nAlpha1 = GETA(color1);
    nAlpha2 = GETA(color2);

    dRed = (float)((nRed2 - nRed1) << 16) / width;
    dGreen = (float)((nGreen2 - nGreen1) << 16) / width;
    dBlue = (float)((nBlue2 - nBlue1) << 16) / width;
    dAlpha = (float)((nAlpha2 - nAlpha1) << 16) / width;

    nRed = nRed1 << 16;
    nGreen = nGreen1 << 16;
    nBlue = nBlue1 << 16; 
    nAlpha = nAlpha1 << 16;

    if (!(GCPAccelerate(pGC) && pGC->device.HWColorFill))
    {
        GCCreateWithMatchingMemoryLocale(pGC, width, 1, &gcScan);
    }

    for (dx = 0; dx < width; dx++)
    {
        GCCOLOR gradient = RGBA((nRed >> 16), (nGreen >> 16), (nBlue >> 16), (nAlpha >> 16));
        if (GCPAccelerate(pGC) && pGC->device.HWColorFill)
        {
            pGC->device.HWColorFill(pGC->device.hDevice, pGC->offsetX + x+dx, pGC->offsetY + y, 1, height, gradient);
        }
        else
        {
            GCSetPixel(&gcScan, dx, 0, gradient);
        }

        nRed += (int)dRed;
        nGreen += (int)dGreen;
        nBlue += (int)dBlue;
        nAlpha += (int)dAlpha;
    }

    if (!(GCPAccelerate(pGC) && pGC->device.HWColorFill))
    {
        for (dy = 0; dy < height; dy++)
        {
            GCCopyBitsXY(pGC, &gcScan, x, y+dy);
        }

        GCDelete(&gcScan);
    }
    GCPEndAccess(pGC);
}

void GCDrawVGradientRectangle(GC *pGC, long x, long y, long width, long height, GCCOLOR color1, GCCOLOR color2)
{
    int nRed1, nRed2, nRed;
    int nGreen1, nGreen2, nGreen;
    int nBlue1, nBlue2, nBlue;
    int nAlpha1, nAlpha2, nAlpha;
    float dRed, dGreen, dBlue, dAlpha;
    long dy, dx;
    GC gcScan;

    if (!width || !height)
        return;

    GCPBeginAccess(pGC);

    if (!(GCPAccelerate(pGC) && pGC->device.HWColorFill))
    {
        GCCreateWithMatchingMemoryLocale(pGC, 1, height, &gcScan);
    }

    nRed1 = GETR(color1);
    nRed2 = GETR(color2);
    nGreen1 = GETG(color1);
    nGreen2 = GETG(color2);
    nBlue1 = GETB(color1);
    nBlue2 = GETB(color2);
    nAlpha1 = GETA(color1);
    nAlpha2 = GETA(color2);

    dRed = (float)((nRed2 - nRed1) << 16) / height;
    dGreen = (float)((nGreen2 - nGreen1) << 16) / height;
    dBlue = (float)((nBlue2 - nBlue1) << 16) / height;
    dAlpha = (float)((nAlpha2 - nAlpha1) << 16) / height;

    nRed = nRed1 << 16;
    nGreen = nGreen1 << 16;
    nBlue = nBlue1 << 16;
    nAlpha = nAlpha1 << 16;

    for (dy = 0; dy < height; dy++)
    {
        GCCOLOR gradient = RGBA((nRed >> 16), (nGreen >> 16), (nBlue >> 16), (nAlpha >> 16));

        if (GCPAccelerate(pGC) && pGC->device.HWColorFill)
        {
            pGC->device.HWColorFill(pGC->device.hDevice, pGC->offsetX + x, pGC->offsetY + y+dy, width, 1, gradient);
        }
        else
        {
            GCSetPixel(&gcScan, 0, dy, gradient);
        }

        nRed += (int)dRed;
        nGreen += (int)dGreen;
        nBlue += (int)dBlue;
        nAlpha += (int)dAlpha;
    }

    if (!(GCPAccelerate(pGC) && pGC->device.HWColorFill))
    {
        for (dx = 0; dx < width; dx++)
        {
            GCCopyBitsXY(pGC, &gcScan, x+dx, y);
        }

        GCDelete(&gcScan);
    }
    GCPEndAccess(pGC);
}

void GCPSwapModesAndColors(GC *pGC)
{
    int gcOpaque;
    int gcGradient;
    int gcOpaqueFG;
    int gcGradientFG;
    GCCOLOR gcBk;
    gcOpaque = pGC->fillMode & GCOPAQUE;
    gcGradient = pGC->fillMode & GCGRADIENT;
    gcOpaqueFG = pGC->fillMode & GCOPAQUEFG;
    gcGradientFG = pGC->fillMode & GCGRADIENTFG;
    pGC->fillMode = 0;
    if (gcOpaque)
    {
        pGC->fillMode |= GCOPAQUEFG;
    }
    if (gcGradient)
    {
        pGC->fillMode |= GCGRADIENTFG;
    }
    if (gcOpaqueFG)
    {
        pGC->fillMode |= GCOPAQUE;
    }
    if (gcGradientFG)
    {
        pGC->fillMode |= GCGRADIENT;
    }
    gcBk = pGC->backgroundColor;
    pGC->backgroundColor = pGC->foregroundColor;
    pGC->foregroundColor = gcBk;
}

void GCPDrawLine(GC *pGC, long x, long y, long x2, long y2, GC* gcpGradient)
{
    int yMajor=0;
    int incrementVal, endVal;
    int i;
    long dx, dy;
    double decInc;
    int shortLen=y2-y;
    int longLen=x2-x;
    int startx, starty;
    double j=0.0;

    startx = MIN(x, x2);
    starty = MIN(y, y2);

    GCPBeginAccess(pGC);
    if (abs(shortLen)>abs(longLen))
    {
        int swap=shortLen;
        shortLen=longLen;
        longLen=swap;
        yMajor=1;
    }

    endVal=longLen;
    if (longLen<0)
    {
        incrementVal=-1;
        longLen=-longLen;
    } 
    else 
        incrementVal=1;

    if (longLen==0)
        decInc=(double)shortLen;
    else 
        decInc=((double)shortLen/(double)longLen);

    if (GCPAccelerate(pGC) &&
    	pGC->device.HWDrawLine &&
    	!(pGC->fillMode & GCGRADIENTFG))
    {
    	pGC->device.HWDrawLine(pGC->device.hDevice, startx, starty, yMajor, decInc, incrementVal, pGC->foregroundColor);
    }
    else
    {
		if (yMajor != 0)
		{
			for (i=0;i!=endVal;i+=incrementVal)
			{
				dx = (long)(x+(int)j);
				dy = (long)(y+i);
				if (pGC->fillMode & GCOPAQUEFG)
				{
					GCSetPixel(pGC, dx, dy, pGC->foregroundColor);
				}
				else if (pGC->fillMode & GCGRADIENTFG)
				{
					GCSetPixel(pGC, dx, dy, GCPSourceGradient(gcpGradient, dx - startx, dy - starty));
				}
				j+=decInc;
			}
		}
		else
		{
			for (i=0; i!=endVal; i+=incrementVal)
			{
				dx = (long)(x+i);
				dy = (long)(y+(int)j);
				if (pGC->fillMode & GCOPAQUEFG)
				{
					GCSetPixel(pGC, dx, dy, pGC->foregroundColor);
				}
				else if (pGC->fillMode & GCGRADIENTFG)
				{
					GCSetPixel(pGC, dx, dy, GCPSourceGradient(gcpGradient, dx - startx, dy - starty));
				}
				j+=decInc;
			}
		}
    }

    GCPEndAccess(pGC);
}

void GCDrawLine(GC *pGC, long x, long y, long x2, long y2)
{
    GC gcGradient;
    int width, height;
    
    width = MAX(x, x2) - MIN(x, x2);
    height = MAX(y, y2) - MIN(y, y2);

    if (width == 0)
        width = 1;
    if (height == 0)
        height = 1;

    if (GRADIENTMODES(pGC->fillMode))
    {
        GCPBeginGradient(pGC, &gcGradient, width, height);
    }

    GCPDrawLine(pGC, x, y, x2, y2, &gcGradient);

    if (GRADIENTMODES(pGC->fillMode))
    {
        GCPEndGradient(&gcGradient);
    }

}

void GCDrawCircle(GC *pGC, long x, long y, long radius)
{
    long f = 1 - radius;
    long ddF_x = 1;
    long ddF_y = -2 * radius;
    long dx = 0;
    long dy = radius;
    GC gcGradient;
    GCPBeginAccess(pGC);

    if (GRADIENTMODES(pGC->fillMode))
    {
        GCPBeginGradient(pGC, &gcGradient, 1 + radius * 2, 1 + radius * 2);
    }

    while(dx < dy + 1)
    {
        if(pGC->fillMode & GCOPAQUE || pGC->fillMode & GCGRADIENT)
        {
            GCPSwapModesAndColors(pGC);
            GCPDrawLine(pGC, x+dx,y+dy,x-dx,y+dy, &gcGradient);
            GCPDrawLine(pGC, x+dx,y-dy,x-dx,y-dy, &gcGradient);
            GCPDrawLine(pGC, x+dy,y+dx,x-dy,y+dx, &gcGradient);
            GCPDrawLine(pGC, x+dy,y-dx,x-dy,y-dx, &gcGradient);
            GCPSwapModesAndColors(pGC);
        }

        if(pGC->fillMode & GCOPAQUEFG || pGC->fillMode & GCGRADIENTFG)
        {
            GCSetPixel(pGC, x+dx,y+dy,pGC->fillMode & GCGRADIENTFG ? GCPSourceGradient(&gcGradient, radius+dx, radius+dy) : pGC->foregroundColor);
            GCSetPixel(pGC, x-dx,y+dy,pGC->fillMode & GCGRADIENTFG ? GCPSourceGradient(&gcGradient, radius-dx, radius+dy) : pGC->foregroundColor);
            GCSetPixel(pGC, x+dx,y-dy,pGC->fillMode & GCGRADIENTFG ? GCPSourceGradient(&gcGradient, radius+dx, radius-dy) : pGC->foregroundColor);
            GCSetPixel(pGC, x-dx,y-dy,pGC->fillMode & GCGRADIENTFG ? GCPSourceGradient(&gcGradient, radius-dx, radius-dy) : pGC->foregroundColor);
            GCSetPixel(pGC, x+dy,y+dx,pGC->fillMode & GCGRADIENTFG ? GCPSourceGradient(&gcGradient, radius+dy, radius+dx) : pGC->foregroundColor);
            GCSetPixel(pGC, x-dy,y+dx,pGC->fillMode & GCGRADIENTFG ? GCPSourceGradient(&gcGradient, radius-dy, radius+dx) : pGC->foregroundColor);
            GCSetPixel(pGC, x+dy,y-dx,pGC->fillMode & GCGRADIENTFG ? GCPSourceGradient(&gcGradient, radius+dy, radius-dx) : pGC->foregroundColor);
            GCSetPixel(pGC, x-dy,y-dx,pGC->fillMode & GCGRADIENTFG ? GCPSourceGradient(&gcGradient, radius-dy, radius-dx) : pGC->foregroundColor);
        }

        if(f >= 0) 
        {
          dy--;
          ddF_y += 2;
          f += ddF_y;
        }
        dx++;
        ddF_x += 2;
        f += ddF_x;    
    }

    if (GRADIENTMODES(pGC->fillMode))
    {
        GCPEndGradient(&gcGradient);
    }

    GCPEndAccess(pGC);
}

char GCPGetFontChar(GC *pGC, char ch, char sub)
{
    if (ch >= pGC->pFont->charStart && ch <= pGC->pFont->charEnd)
        return ((char*)pGC->pFont->pFont)[((ch - pGC->pFont->charStart) * pGC->pFont->maxCharWidth) + sub];
    return 0xFF;
}

long GCPGetFontCharWidth(GC *pGC, char ch)
{
    return pGC->pFont->maxCharWidth;
}

long GCPFindCharIndex(char *array, char ch)
{
    char *start = array;
    long maybe = -1;
    long definitely = -1;
    char alpha = isalpha(ch);
    do
    {
        if (*array == ch)
        {
            definitely = array - start;
            break;
        }
        else if (alpha && isalpha(*array) && (ch == toupper(*array) || ch == tolower(*array)))
        {
            maybe = array - start;
        }
    } while (*(char*)(++array));

    if (definitely > -1)
        return definitely;

    return maybe;
}

void GCPDrawCharEnc2(GC *pGC, char chdraw, long startx, long starty, GC *pGCGradient)
{
    char index = 0;
    long endx, endy;
    startx = MIN(startx, pGC->bitmap.width + -pGC->offsetX);
    starty = MIN(starty, pGC->bitmap.height);
    endx = MIN(startx + pGC->pFont->maxCharWidth, pGC->bitmap.width + -pGC->offsetX);
    endy = MIN(starty + pGC->pFont->charHeight, pGC->bitmap.height);

    if (startx <= -pGC->pFont->maxCharWidth ||
        starty <= -pGC->pFont->charHeight)
        return;
    GCBITMAPFONT *bitmapFont = (GCBITMAPFONT*)pGC->pFont->pFont;
    long loc = GCPFindCharIndex(bitmapFont->alpha, chdraw);
    if (loc != -1)
    {
        GC gcChar;
        GCRECT rect = { 0, 0, pGC->pFont->maxCharWidth, pGC->pFont->charHeight };
        //GCCreateWithPreallocatedMemory(pGC, bitmapFont->columns * pGC->pFont->maxCharWidth, bitmapFont->rows * pGC->pFont->charHeight, bitmapFont->bits, &gcChar);
        long row = loc / bitmapFont->columns;
        long col = loc % bitmapFont->columns;
        OFFSETRECT((&rect), (col * pGC->pFont->maxCharWidth), (row * pGC->pFont->charHeight));
        GCPOINT pt = { startx, starty };
        GCCopyBits2(pGC, (GC*)bitmapFont->bits, &rect, &pt);
    }
}

void GCPDrawCharEnc1(GC *pGC, char chdraw, long startx, long starty, GC *pGCGradient)
{
    char index = 0;
    long endx, endy;
    long x, y;
    startx = MIN(startx, pGC->bitmap.width + -pGC->offsetX);
    starty = MIN(starty, pGC->bitmap.height);
    endx = MIN(startx + pGC->pFont->maxCharWidth, pGC->bitmap.width + -pGC->offsetX);
    endy = MIN(starty + pGC->pFont->charHeight, pGC->bitmap.height);

    if (startx <= -pGC->pFont->maxCharWidth ||
        starty <= -pGC->pFont->charHeight)
        return;

    for (x = startx; x < endx; x++)
    {
        int step = 0;
        for (y = starty; y < endy; y++)
        {
            int nibble = GCPGetFontChar(pGC, chdraw, index);
            if (((nibble >> step) & 1) == 1)
            {
                if (pGC->fillMode & GCGRADIENTFG)
                {
                    GCSetPixel(pGC, x, y, GCPSourceGradient(pGCGradient, index, step));
                }
                else if (pGC->fillMode & GCOPAQUEFG)
                {
                    GCSetPixel(pGC, x, y, pGC->foregroundColor);
                }
            }
            else if (pGC->fillMode & GCGRADIENT)
            {
                GCSetPixel(pGC, x, y, GCPSourceGradient(pGCGradient, index, step));
            }
            else if(pGC->fillMode & GCOPAQUE)
            {
                GCSetPixel(pGC, x, y, pGC->backgroundColor);
            }
            step++;
        }
        index++;
    }
}

void GCPDrawCharEnc0(GC *pGC, char chdraw, long startx, long starty, GC *pGCGradient)
{
    char index = 0;
    long endx, endy;
    long x, y;
    startx = MIN(startx, pGC->bitmap.width+-pGC->offsetX);
    starty = MIN(starty, pGC->bitmap.height);
    endx = MIN(startx + pGC->pFont->maxCharWidth, pGC->bitmap.width+-pGC->offsetX);
    endy = MIN(starty + pGC->pFont->charHeight, pGC->bitmap.height);

    if (startx <= -pGC->pFont->maxCharWidth ||
        starty <= -pGC->pFont->charHeight)
        return;

    for (y = starty; y < endy; y++)
    {
        int nibble = GCPGetFontChar(pGC, chdraw, index);
        int step = pGC->pFont->charHeight-1;
        for (x = startx; x < endx; x++)
        {
            if (((nibble >> step) & 1) == 1)
            {
                if (pGC->fillMode & GCGRADIENTFG)
                {
                    GCSetPixel(pGC, x, y, GCPSourceGradient(pGCGradient, step, index));
                }
                else if (pGC->fillMode & GCOPAQUEFG)
                {
                    GCSetPixel(pGC, x, y, pGC->foregroundColor);
                }
            }
            else if (pGC->fillMode & GCGRADIENT)
            {
                GCSetPixel(pGC, x, y, GCPSourceGradient(pGCGradient, step, index));
            }
            else if(pGC->fillMode & GCOPAQUE)
            {
                GCSetPixel(pGC, x, y, pGC->backgroundColor);
            }
            step--;
        }
        index++;
    }
}

void GCDrawText(GC *pGC, char text[], long startx, long starty, long endx)
{
    GC gcGradient;
    long deltax = 0;
    unsigned long i;
    GCPBeginAccess(pGC);
    if (GRADIENTMODES(pGC->fillMode))
    {
        long sx, sy;
        if (pGC->pFont->encodingType == 0)
        {
            sx = pGC->pFont->charHeight;
            sy = pGC->pFont->maxCharWidth;
        }
        else
        {
            sx = pGC->pFont->maxCharWidth;
            sy = pGC->pFont->charHeight;
        }

        GCPBeginGradient(pGC, &gcGradient, sx, sy);
    }

    if (endx == 0)
    {
        endx = pGC->bitmap.width;
    }

    endx += -pGC->offsetX;

    for (i = 0; i < strlen(text); i++)
    {
        if (text[i] == '\n' ||
        ((startx + (deltax * pGC->pFont->maxCharWidth) + pGC->pFont->maxCharWidth) > endx)
         && pGC->textMode == GCTEXT_WRAP)
        {
            deltax = 0;
            starty+=pGC->pFont->charHeight;
        }
        if (text[i] == '\n')
            continue;
        if (text[i] == '\0')
            break;
        switch(pGC->pFont->encodingType)
        {
        case 0:
            GCPDrawCharEnc0(pGC, text[i], startx + (deltax * GCPGetFontCharWidth(pGC, text[i])), starty, &gcGradient);
            break;
        case 1:
            GCPDrawCharEnc1(pGC, text[i], startx + (deltax * GCPGetFontCharWidth(pGC, text[i])), starty, &gcGradient);
            break;
        case 2:
            GCPDrawCharEnc2(pGC, text[i], startx + (deltax * GCPGetFontCharWidth(pGC, text[i])), starty, &gcGradient);
            break;
        default:
            break;
        }
        deltax++;
    }

    if (GRADIENTMODES(pGC->fillMode))
    {
        GCPEndGradient(&gcGradient);
    }
    GCPEndAccess(pGC);
}

void GCRotateSurface(GC *pGC, long degrees)
{
    GC gcCopy;
    long x, y;
    if (degrees >= 360 || degrees == 0)
        return;
    GCPBeginAccess(pGC);
    GCCreateWithMatchingMemoryLocale(pGC, pGC->bitmap.width, pGC->bitmap.height, &gcCopy);
    GCCopyBits(&gcCopy, pGC);

    for (x = 0; x < pGC->bitmap.width; x++)
    {
        for (y = 0; y < pGC->bitmap.height; y++)
        {
            long x2 = 0, y2 = 0;
            switch(degrees)
            {
                case 90:
                    y2 = x;
                    x2 = pGC->bitmap.width - y - 1;
                    break;
                case 180:
                    y2 = pGC->bitmap.height - x - 1;
                    x2 = pGC->bitmap.width - y - 1;
                    break;
                case 270:
                    y2 = pGC->bitmap.height - x - 1;
                    x2 = y;
                    break;
            }

            GCSetPixel(pGC, x2, y2, GCGetPixel(&gcCopy, x, y));
        }
    }
    GCDelete(&gcCopy);
    GCPEndAccess(pGC);
}


GCCOLOR GCPDECCOLOR(GCCOLOR col)
{
    char r = GETR(col);
    char g = GETG(col);
    char b = GETB(col);
    if (r) r--;
    if (g) g--;
    if (b) b--;
    return RGB(r, g, b);
}

void GCPBeginGradient(GC *pGCSource, GC *pGC, long width, long height)
{
    if (pGCSource->pGCCacheGradient == NULL)
    {
        GCCreateWithMatchingMemoryLocale(pGCSource, width, height, pGC);
        switch (pGCSource->gradientFillMode)
        {
        default:
        case GCHGRADIENT:
            GCDrawHGradientRectangle(pGC, 0, 0, width, height, pGCSource->gradientStart, pGCSource->gradientFinish);
            break;
        case GCVGRADIENT:
            GCDrawVGradientRectangle(pGC, 0, 0, width, height, pGCSource->gradientStart, pGCSource->gradientFinish);
            break;
        }
    }
    else
    {
        *pGC = *pGCSource->pGCCacheGradient;
    }
}

GCCOLOR GCPSourceGradient(GC *pGC, long x, long y)
{
    return GCGetPixel(pGC, x, y);
}

void GCPEndGradient(GC *pGC)
{
    if (pGC->pGCCacheGradient == NULL)
    {
        GCDelete(pGC);
    }
}

void GCBeginCacheGradient(GC *pGCSource, GC *pGC, long width, long height)
{
    GCPBeginGradient(pGCSource, pGC, width, height);
    pGCSource->pGCCacheGradient = pGC;
    pGC->pGCCacheGradient = pGCSource;
}

void GCEndCacheGradient(GC *pGC)
{
    GC *pGCCacheGradient = pGC->pGCCacheGradient;
    pGC->pGCCacheGradient->pGCCacheGradient = NULL;
    pGC->pGCCacheGradient = NULL;
    GCPEndGradient(pGCCacheGradient);
}

void GCDimBuffer(GC *pGC)
{
    long x, y;
    GCPBeginAccess(pGC);
    if (GCPAccelerate(pGC) && pGC->device.HWDimBuffer)
    {
        pGC->device.HWDimBuffer(pGC->device.hDevice);
    }
    else
    {
        for (y = 0; y < pGC->bitmap.height; y++)
        {
            for (x = 0; x < pGC->bitmap.width; x++)
            {
                GCSetPixel(pGC, x, y, GCPDECCOLOR(GCGetPixel(pGC, x, y)));
            }
        }
    }
    GCPEndAccess(pGC);
}

GCCOLOR GCPComputeAlpha(GCCOLOR colorSrc, GCCOLOR colorDst, GCCOLOR alpha)
{
    GCCOLOR invalpha;
    invalpha = 255 - alpha;
    return RGB((GETR(colorDst) * invalpha + GETR(colorSrc) * alpha) / 255,
               (GETG(colorDst) * invalpha + GETG(colorSrc) * alpha) / 255,
               (GETB(colorDst) * invalpha + GETB(colorSrc) * alpha) / 255);
}

void GCCopyBits2(GC *pGC, GC *pGCSrc, GCRECT *prcSrc, GCPOINT *pptDst)
{
    long xDst = 0;
    long yDst = 0;
    long x, y;
    if (EMPTYRECT(prcSrc) || pptDst->x >= pGC->bitmap.width || pptDst->y >= pGC->bitmap.height)
    {
        return;
    }

    GCPBeginAccess(pGC);
    GCPBeginAccess(pGCSrc);
    if (pGCSrc == pGC)
    {
        if (WITHINRECT(prcSrc, pptDst->x, pptDst->y) ||
            WITHINRECT(prcSrc, pptDst->x + RECTWIDTH(prcSrc), pptDst->y + RECTHEIGHT(prcSrc)))
        {
            GC gcCopy;
            if (GCCreateWithMatchingMemoryLocale(pGCSrc, RECTWIDTH(prcSrc), RECTHEIGHT(prcSrc), &gcCopy))
            {
                GCPOINT ptDstCopy = {0, 0};
                GCCopyBits2(&gcCopy, pGC, prcSrc, &ptDstCopy);
                gcCopy.blitEffect = pGCSrc->blitEffect;
                GCCopyBitsXY(pGC, &gcCopy, pptDst->x, pptDst->y);
                GCDelete(&gcCopy);
            }
            goto finish;
        }
    }

    // See if we can try and accelerate this call:
    if (pGCSrc->bitmap.prgBits != NULL ||
        pGCSrc->bitmap.disposition == GCDeviceDisplayOnly ||
        pGCSrc->bitmap.disposition == GCDeviceMemory)
    {
        if (GCPAccelerate(pGC) && 
            pGCSrc->blitEffect.EffectType == GCBLIT_NORMAL &&
            pGC->device.HWCopyBits != NULL)
        {
            pGC->device.HWCopyBits(pGC->device.hDevice,
                                   pptDst->x + pGC->offsetX,
                                   pptDst->y + pGC->offsetY,
                                   MIN(RECTWIDTH(prcSrc), pGCSrc->bitmap.width),
                                   MIN(RECTHEIGHT(prcSrc), pGCSrc->bitmap.height),
                                   &pGCSrc->bitmap,
                                   prcSrc->left,
                                   prcSrc->top,
                                   MIN(RECTWIDTH(prcSrc), pGCSrc->bitmap.width),
                                   MIN(RECTHEIGHT(prcSrc), pGCSrc->bitmap.height),
                                   0);
            goto finish;
        }
        if (GCPAccelerate(pGC) && 
            pGCSrc->blitEffect.EffectType == GCBLIT_CONSTANTALPHA && 
            pGC->device.HWConstantAlphaBlend != NULL)
        {
            pGC->device.HWConstantAlphaBlend(pGC->device.hDevice,
                                             pptDst->x + pGC->offsetX,
                                             pptDst->y + pGC->offsetY,
                                             MIN(RECTWIDTH(prcSrc), pGCSrc->bitmap.width),
                                             MIN(RECTHEIGHT(prcSrc), pGCSrc->bitmap.height),
                                             &pGCSrc->bitmap,
                                             prcSrc->left,
                                             prcSrc->top,
                                             MIN(RECTWIDTH(prcSrc), pGCSrc->bitmap.width),
                                             MIN(RECTHEIGHT(prcSrc), pGCSrc->bitmap.height),
                                             pGCSrc->blitEffect.param);
            goto finish;
        }
        if (GCPAccelerate(pGC) && 
            pGCSrc->blitEffect.EffectType == GCBLIT_COLORKEY && 
            pGC->device.HWTransparentBlt != NULL)
        {
            pGC->device.HWTransparentBlt(pGC->device.hDevice,
                                         pptDst->x + pGC->offsetX,
                                         pptDst->y + pGC->offsetY,
                                         MIN(RECTWIDTH(prcSrc), pGCSrc->bitmap.width),
                                         MIN(RECTHEIGHT(prcSrc), pGCSrc->bitmap.height),
                                         &pGCSrc->bitmap,
                                         prcSrc->left,
                                         prcSrc->top,
                                         MIN(RECTWIDTH(prcSrc), pGCSrc->bitmap.width),
                                         MIN(RECTHEIGHT(prcSrc), pGCSrc->bitmap.height),
                                         pGCSrc->blitEffect.param,
                                         0);
            goto finish;
        }
    }
    for (x = prcSrc->left; x < prcSrc->right; x++)
    {
        for (y = prcSrc->top; y < prcSrc->bottom; y++)
        {
            if (pGCSrc->blitEffect.EffectType == GCBLIT_NORMAL)
            {
                GCSetPixel(pGC, pptDst->x + xDst, pptDst->y + yDst, GCGetPixel(pGCSrc, x, y));
            }
            else
            {
                GCCOLOR colorSrc, colorDst;
                GCBOOL fSkip = GCFALSE;
                colorSrc = GCGetPixel(pGCSrc, x, y);
                colorDst = 0;
                
                switch(pGCSrc->blitEffect.EffectType)
                {
                case GCBLIT_COLORKEY:
                    if (colorSrc != pGCSrc->blitEffect.param)
                    {
                        colorDst = colorSrc;
                    }
                    else
                    {
                        fSkip = GCTRUE;
                    }
                    break;
                case GCBLIT_CONSTANTALPHA:
                    if (pGCSrc->blitEffect.param == 0xFF)
                    {
                        colorDst = colorSrc;
                    }
                    else if (pGCSrc->blitEffect.param == 0)
                    {
                        fSkip = GCTRUE;
                    }
                    else
                    {
                        colorDst = GCPComputeAlpha(colorSrc, GCGetPixel(pGC, pptDst->x + xDst, pptDst->y + yDst), pGCSrc->blitEffect.param);
                    }
                    break;
                case GCBLIT_PERPIXELALPHA:
                    if (GETA(colorSrc) == 0xFF)
                    {
                        colorDst = colorSrc & (RMASK | GMASK | BMASK);
                    }
                    else if (GETA(colorSrc) == 0)
                    {
                        fSkip = GCTRUE;
                    }
                    else
                    {
                        colorDst = GCPComputeAlpha(colorSrc, GCGetPixel(pGC, pptDst->x + xDst, pptDst->y + yDst), GETA(colorSrc));
                    }
                    break;
                }
                if (fSkip == GCFALSE)
                {
                    GCSetPixel(pGC, pptDst->x + xDst, pptDst->y + yDst, colorDst);
                }
            }
            yDst++;
        }
        xDst++;
        yDst = 0;
    }
finish:
    GCPEndAccess(pGCSrc);
    GCPEndAccess(pGC);
}

void GCCopyBitsXY(GC *pGC, GC *pGCSrc, long xDst, long yDst)
{
    GCRECT rcSrc = {0, 0, GCWidth(pGCSrc), GCHeight(pGCSrc)};
    GCPOINT ptDst = {xDst, yDst};
    GCCopyBits2(pGC, pGCSrc, &rcSrc, &ptDst);
}

void GCCopyBits(GC *pGC, GC *pGCSrc)
{
    GCPOINT pt = {0, 0};
    GCRECT rcSrc = {0, 0, GCWidth(pGCSrc), GCHeight(pGCSrc)};
    GCCopyBits2(pGC, pGCSrc, &rcSrc, &pt);
}

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
  )
{
    GCPBeginAccess(pGC);
    GCPBeginAccess(pGCSrc);
#if 1
    if (GCPAccelerate(pGC))
	{
        if (pGCSrc->blitEffect.EffectType == GCBLIT_NORMAL &&
        	pGC->device.HWCopyBits != NULL)
		{
			pGC->device.HWCopyBits(pGC->device.hDevice,
								   nXOriginDest + pGC->offsetX,
								   nYOriginDest + pGC->offsetY,
								   nWidthDest,
								   nHeightDest,
								   &pGCSrc->bitmap,
								   nXOriginSrc,
								   nYOriginSrc,
								   nWidthSrc,
								   nHeightSrc,
								   0);
			goto finish;
		}
        else if (pGCSrc->blitEffect.EffectType == GCBLIT_COLORKEY &&
        	pGC->device.HWTransparentBlt != NULL)
        {
			pGC->device.HWTransparentBlt(pGC->device.hDevice,
										   nXOriginDest + pGC->offsetX,
										   nYOriginDest + pGC->offsetY,
										   nWidthDest,
										   nHeightDest,
										   &pGCSrc->bitmap,
										   nXOriginSrc,
										   nYOriginSrc,
										   nWidthSrc,
										   nHeightSrc,
										   pGCSrc->blitEffect.param,
										   0);
        	goto finish;
        }
	}
#endif
    /*
	state 0:
	    int x_ratio = (int)((wSrc<<16)/wDest) +1;
	    int y_ratio = (int)((hSrc<<16)/hDest) +1;
	state 1:
	    int SrcX = ((dx*x_ratio)>>16) ;
	    int SrcY = ((dy*y_ratio)>>16) ;
*/
    int x_ratio = (int)((nWidthSrc<<16)/nWidthDest) +1;
    int y_ratio = (int)((nHeightSrc<<16)/nHeightDest) +1;
    int x, y;
    for (y = nYOriginDest; y < nYOriginDest + nHeightDest; y++)
    {
    	for (x = nXOriginDest; x < nXOriginDest + nWidthDest; x++)
        {
    	    int SrcX = (((x - nXOriginDest)*x_ratio)>>16) ;
    	    int SrcY = (((y - nYOriginDest)*y_ratio)>>16) ;
    	    GCCOLOR color = GCGetPixel(pGCSrc, SrcX, SrcY);
    	    if (pGCSrc->blitEffect.EffectType == GCBLIT_NORMAL || (pGCSrc->blitEffect.param & 0xFFFFFF) != (color & 0xFFFFFF))
    	    {
				GCSetPixel(pGC, x, y, color);
    	    }
        }
    }

finish:
	GCPEndAccess(pGCSrc);
	GCPEndAccess(pGC);
}

#define GCONE (1 << 16)
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
  )
{
    GCPBeginAccess(pGC);
    GCPBeginAccess(pGCSrc);

    GCRECT bounds;
    GCPOINT poly[4] = {{0, 0}, {0,nHeightSrc}, {nWidthSrc, 0}, {nWidthSrc, nHeightSrc}};
    GCPOINT origin = {nXOriginDest + pGC->offsetX, nYOriginDest + pGC->offsetY};
    GCMxGetPolyBounds(pGC, &bounds, poly, 4, pMatrix, &origin);

    GCMATRIX inverse;
    GCMxInverse(pMatrix, &inverse);

#if 1
    if (GCPAccelerate(pGC))
	{
    	GCMATRIX matrix;
    	GCMATRIX inversewithxlate = inverse;
    	GCMxTranslate(&matrix, bounds.left - nXOriginDest, bounds.top - nYOriginDest);
    	GCMxMultiply(&inversewithxlate, &matrix, &inverse);

    	if (pGCSrc->blitEffect.EffectType == GCBLIT_NORMAL &&
        	pGC->device.HWCopyBits != NULL)
		{
			pGC->device.HWCopyBits(pGC->device.hDevice,
								   bounds.left,
								   bounds.top,
								   bounds.right - bounds.left,
								   bounds.bottom - bounds.top,
								   &pGCSrc->bitmap,
								   nXOriginSrc,
								   nYOriginSrc,
								   nWidthSrc,
								   nHeightSrc,
								   &inversewithxlate);
			goto finish;
		}
        else if (pGCSrc->blitEffect.EffectType == GCBLIT_COLORKEY &&
        	pGC->device.HWTransparentBlt != NULL)
        {
			pGC->device.HWTransparentBlt(pGC->device.hDevice,
					bounds.left,
					bounds.top,
					bounds.right - bounds.left,
					bounds.bottom - bounds.top,
					&pGCSrc->bitmap,
					nXOriginSrc,
					nYOriginSrc,
					nWidthSrc,
					nHeightSrc,
					pGCSrc->blitEffect.param,
					&inversewithxlate);
        	goto finish;
        }
	}
#endif
    int x, y;
    for (y = bounds.top; y < bounds.bottom; y++)
    {
    	for (x = bounds.left; x < bounds.right; x++)
        {
    		if (x >= 0 && x < GCWidth(pGC) &&
	    		y >= 0 && y < GCHeight(pGC))
	    	{
				GCPOINT Source;
				Source.x = x - nXOriginDest;
				Source.y = y - nYOriginDest;
				GCMxTransformPoint(&Source, &inverse);
				Source.x += nXOriginSrc;
				Source.y += nYOriginSrc;

				if (Source.x >= nXOriginSrc && MIN(Source.x < nXOriginSrc + nWidthSrc, GCWidth(pGCSrc)) &&
					Source.y >= nYOriginSrc && MIN(Source.y < nYOriginSrc + nHeightSrc, GCHeight(pGCSrc)))
				{
					GCCOLOR color = GCGetPixel(pGCSrc, Source.x, Source.y);
					if (pGCSrc->blitEffect.EffectType == GCBLIT_NORMAL || (pGCSrc->blitEffect.param & 0xFFFFFF) != (color & 0xFFFFFF))
					{
						GCSetPixel(pGC, x, y, color);
					}
				}
	    	}
        }
    }

finish:
	GCPEndAccess(pGCSrc);
	GCPEndAccess(pGC);
}

void GCStretchBits2(
  GC *pGCDst,
  GCRECT *prcDst,
  GC *pGCSrc,
  GCRECT *prcSrc
  )
{
	GCStretchBits(pGCDst,
				  prcDst->left,
				  prcDst->top,
				  GCWidth(pGCDst),
				  GCHeight(pGCDst),
				  pGCSrc,
				  prcSrc->left,
				  prcSrc->top,
				  GCWidth(pGCSrc),
				  GCHeight(pGCSrc));
}

void GCBurnPixel(GC *pGC, long x, long y, GCCOLOR color)
{
    GCSetPixel(pGC, x, y, color | GCGetPixel(pGC, x, y));
}

void GCSetPixel(GC *pGC, long x, long y, GCCOLOR color)
{
    GCCOLOR notMask = 0;
    GCPBeginAccess(pGC);
    if (pGC->colorMask != (AMASK|GMASK|BMASK|RMASK))
    {
        notMask = pGC->colorMask ^ (AMASK|GMASK|BMASK|RMASK);
        color = (color & pGC->colorMask) | (pGC->device.HWGetPixel(pGC->device.hDevice,  pGC->offsetX + x, pGC->offsetY + y) & notMask);
    }
    pGC->device.HWSetPixel(pGC->device.hDevice, pGC->offsetX + x, pGC->offsetY + y, color);
    GCPEndAccess(pGC);
}

GCCOLOR GCGetPixel(GC *pGC, long x, long y)
{
    GCCOLOR color;
    GCPBeginAccess(pGC);
    color = pGC->device.HWGetPixel(pGC->device.hDevice, pGC->offsetX + x, pGC->offsetY + y);
    color &= pGC->colorMask;
    GCPEndAccess(pGC);
    return color;
}
