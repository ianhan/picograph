#include <stdio.h>
#include "pico/stdlib.h"
#include "gc.h"
#include "libdlo.h"
//#include "mdafont.h"

#define DLO_HANDLE(bitmap) ((dlo_dev_t)(uintptr_t)((bitmap)->handle))

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
    dlo_rect_t rec;
    rec.height = 1;
    rec.width = 1;
    rec.origin.x = x;
    rec.origin.y = y;

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
    dlo_rect_t rec;
    rec.height = cy - 1;
    rec.width = cx - 1;
    rec.origin.x = x;
    rec.origin.y = y;

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
    dlo_set_mode(param, &desc);

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
    dlo_mode_t *pMode = dlo_get_mode(param);
    GCInitializeBitmap(pDisplaySurface, pMode->view.width, pMode->view.height, (unsigned long)(uintptr_t)param, GCDeviceDisplayOnly);
    return GCTRUE;
}

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


GCBITMAPFONT slimebitmapFont =
{
    10,
    6,
    " !     '()  , . 0123456789     ? ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    NULL
};
GCFONT slimefont = { &slimebitmapFont, 32, 32, 32, 126, 2 };

GCBITMAPFONT crybitmapFont =
{
    5,
    8,
    "abcdefghijklmnopqrstuvwxyz23456789-:.,()",
    NULL
};
GCFONT cryfont = { &crybitmapFont, 64, 57, 32, 126, 2 };

GCBITMAPFONT otherbitmapFont =
{
    10,
    4,
    "abcdefghijklmmnopqrstuvwwxyz**",
    NULL
};
GCFONT OTHERfont = { &otherbitmapFont, 32, 39, 32, 126, 2 };

void drawCursor(GC *pGC, long x, long y)
{
    GCDrawLine(pGC, x, y, x, y+15);
    GCDrawLine(pGC, x, y, x+11, y+12);
    GCDrawLine(pGC, x, y+15, x+4, y+13);
    GCDrawLine(pGC, x+4, y+13, x+8, y+20);
    GCDrawLine(pGC, x+8, y+20, x+9, y+18);
    GCDrawLine(pGC, x+9, y+18, x+6, y+12);
    GCDrawLine(pGC, x+6, y+12, x+11, y+12);
}

void GCDrawRandomGlyphs(GC* pGC)
{
    uint32_t xglyphs = GCWidth(pGC) / 8;
    uint32_t yglyphs = GCHeight(pGC) / 8;
    uint32_t width = GCWidth(pGC);
    uint32_t lastx = -1;
    uint32_t lasty = -1;
#if 0
        int r, g, b;
        r = ((unsigned char*)&colortable_+3*val)[0];
        g = ((unsigned char*)&colortable_+3*val)[1];
        b = ((unsigned char*)&colortable_+3*val)[2];
        GCSetForegroundColor(pGC, (r << 16 | g << 8 | b));
#endif
    GCSetForegroundColor(pGC, RGB(0, 255, 0));
    while(1)
    {
        GCPBeginAccess(pGC);
        //if (lastx != -1)
          //  GCFastFill(pGC, lastx, lasty, 8, 8, 0);
        unsigned char val = rand()%255;
        char sz[] = {val, 0};
        lastx = (rand()%xglyphs)*8;
        lasty = (rand()%yglyphs)*8;
        GCDrawText(pGC, sz, lastx, lasty, width);
        GCPEndAccess(pGC);
    }
}

uint32_t syspalette2[256];
uint32_t syspalette[256];
uint32_t palette_data;
uint8_t palette_index;
uint8_t palette_position;
uint8_t palette_mask = 0xff;
void pal_dac_mask(uint8_t mask)
{
    palette_mask = mask;
}

void pal_dac_write(uint8_t index)
{
    palette_index = index;
    palette_position = 0;
}
#define PAL6_TO_8(d) ((((uint32_t)d * 259 + 33) >> 6) & 0xFF)
void pal_dac_data(uint8_t data)
{
    // Check if the current index is masked
    switch(palette_position)
    {
        case 0:
            palette_data = ((data * 259 + 33) >> 6) & 0xFF;
            palette_position++;
            break;
        case 1:
            palette_data |= (((data * 259 + 33) >> 6) & 0xFF) << 8;
            palette_position++;
            break;
        case 2:
            palette_position = 0;
            if ((palette_mask & (1 << (palette_index & 7)))) {
                syspalette[palette_index] = palette_data | PAL6_TO_8(data) << 16;
            }
            palette_index++;
            break;
    }
}

uint8_t post_code;
uint8_t post_code_current;
uint8_t post_code_history[4];
uint8_t post_code_history_count;
uint8_t post_code_history_next;
bool post_code_dirty = true;
void post_code_write(uint8_t value)
{
    post_code = post_code_current;
    post_code_current = value;
    post_code_history[post_code_history_next] = value;
    post_code_history_next = (post_code_history_next + 1) & 0x03;
    if (post_code_history_count < 4) {
        post_code_history_count++;
    }
    post_code_dirty = true;
}


GC gc;

void DrawPalette(GC *pGC, long paletteX, long paletteY, long paletteWidth, long paletteHeight)
{
    uint8_t uiPalOffset = 0;
    long uiBorderWidth;
    long uiCellSizeX, uiCellSizeY;
    long fillWidth, fillHeight;

    uiCellSizeX = paletteWidth/16;
    uiCellSizeY = paletteHeight/16;
    if (uiCellSizeX < 1 || uiCellSizeY < 1) {
        return;
    }
    if (uiCellSizeX > 1) {
        uiCellSizeX &= ~1L;
    }
    if (uiCellSizeY > 1) {
        uiCellSizeY &= ~1L;
    }

    uiBorderWidth = paletteHeight/100;
    long minCellSize = uiCellSizeX < uiCellSizeY ? uiCellSizeX : uiCellSizeY;
    if (uiBorderWidth < 1 && minCellSize > 2) {
        uiBorderWidth = 1;
    }
    if (uiBorderWidth >= minCellSize) {
        uiBorderWidth = minCellSize > 1 ? minCellSize - 1 : 0;
    }

    fillWidth = uiCellSizeX - uiBorderWidth;
    fillHeight = uiCellSizeY - uiBorderWidth;
    if (fillWidth < 1) {
        fillWidth = 1;
    }
    if (fillHeight < 1) {
        fillHeight = 1;
    }

    GCSetOffset(pGC, paletteX, paletteY);

    bool drewsomething = false;
    for (int y = 0; y < 16; y++)
    {
        GCPBeginAccess(pGC);
        for (int x = 0; x < 16; x++)
        {
            uint32_t cur = syspalette[uiPalOffset];
            if (cur != syspalette2[uiPalOffset])
            {
                syspalette2[uiPalOffset] = cur;
                long xpos, ypos;
                xpos = x*uiCellSizeX;
                ypos = y*uiCellSizeY;
                GCFastFill(pGC, xpos+uiBorderWidth, ypos+uiBorderWidth, fillWidth, fillHeight, syspalette2[uiPalOffset]);
                drewsomething = true;
            }
            uiPalOffset++;
        }
        GCPEndAccess(pGC);
    }
    GCSetOffset(pGC, 0, 0);
}

void DrawPostCodes(GC *pGC, long paletteX, long paletteY, long paletteWidth)
{
    const char hex[] = "0123456789ABCDEF";
    char text[] = "POST -- -- -- --";
    long textHeight = pGC->pFont ? pGC->pFont->charHeight : 8;
    long textY = paletteY - textHeight - 2;
    if (textY < 0) {
        textY = paletteY + 2;
    }

    for (uint8_t i = 0; i < post_code_history_count; ++i) {
        uint8_t slot = (post_code_history_next + 4 - post_code_history_count + i) & 0x03;
        uint8_t value = post_code_history[slot];
        uint8_t text_offset = 5 + i*3;
        text[text_offset] = hex[value >> 4];
        text[text_offset + 1] = hex[value & 0x0f];
    }

    GCCOLOR oldBackground = pGC->backgroundColor;
    GCCOLOR oldForeground = pGC->foregroundColor;
    long oldFillMode = pGC->fillMode;
    long oldTextMode = pGC->textMode;

    GCSetBackgroundColor(pGC, RGB(0, 0, 0));
    GCSetForegroundColor(pGC, RGB(255, 255, 255));
    GCSetFillMode(pGC, GCOPAQUE | GCOPAQUEFG);
    GCSetTextMode(pGC, GCTEXT_SINGLELINE);
    GCDrawText(pGC, text, paletteX, textY, paletteX + paletteWidth);

    GCSetTextMode(pGC, oldTextMode);
    GCSetFillMode(pGC, oldFillMode);
    GCSetForegroundColor(pGC, oldForeground);
    GCSetBackgroundColor(pGC, oldBackground);
}

void Task_UpdatePalette()
{
    if (gc.bitmap.handle) {
        GC* pGC = &gc;

        long paletteWidth = 128;
        long paletteHeight = 128;
        long paletteX = GCWidth(pGC) - paletteWidth;
        long paletteY = 16;

        if (post_code_dirty) {
            DrawPostCodes(&gc, paletteX, paletteY, paletteWidth);
            post_code_dirty = false;
        }
        DrawPalette(&gc, paletteX, paletteY, paletteWidth, paletteHeight);
        dlo_flush_usb(DLO_HANDLE(&pGC->bitmap), true);
    }
}

void dlo_device_configured (dlo_dev_t uid)
{
    GC gcFont = {0};
    printf("gc: dlo device configured\n");
    GCDisplayLinkCreate(&gc, uid);
    GCFastFill(&gc, 0, 0, GCWidth(&gc), GCHeight(&gc), 0);
}
