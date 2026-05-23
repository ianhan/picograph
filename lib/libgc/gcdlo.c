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
void setupVectors(GC *pGC);
void drawVectors(GC *pGC);





#include <math.h>

#define random rand
unsigned char colortable_[256 * 3 * 2];  // 256 colors * {rgb} * 2 cycles

typedef struct palette_list_struct *palette_list_ptr;
typedef struct palette_struct *palette_ptr;


void delete_palette_list( palette_list_ptr p );

int get_number_of_palettes( palette_list_ptr p );
palette_ptr get_palette( palette_list_ptr p, int idx );
char* get_name(palette_ptr p);

void get_colortable( palette_ptr p,
                     unsigned char *ctable,
                     int randomize,
                     int stripes);


/** holds the red/green/blue components for a color */
struct rgcolor_struct
{
  unsigned char r;  /**< red component */
  unsigned char g;  /**< green component */
  unsigned char b;  /**< green component */
};
typedef struct rgcolor_struct rgcolor;


/** holds a list of palettes */
struct palette_list_struct
{
  int  num_palettes;    /**< number of palettes */
  palette_ptr palettes; /**< the palette data */
};
typedef struct palette_list_struct palette_list;


/** holds data for a single palette */
struct palette_struct
{
  int  num_colors;             /**< number of colors in the palette */
  rgcolor  *colors;           /**< the color data */
};
typedef struct palette_struct palette;

typedef struct tagPALETTEDATA
{
    int num_colors;
    unsigned long colors[10];
} PALETTEDATA, *PPALETTEDATA;

palette_list_ptr paletteList_;

PALETTEDATA palettedata[] =
{
    {3, {0xFF0000, 0x00FF00, 0x0000FF, 0, 0, 0, 0, 0, 0, 0}}
};


palette_list_ptr init_palette_list()
{
  int ii;

  palette_list_ptr pl = (palette_list_ptr)malloc(sizeof(palette_list));

  /* get the number of palettes */
  pl->num_palettes = sizeof(palettedata) / sizeof(PALETTEDATA);
  pl->palettes = (palette_ptr)malloc( sizeof(palette) * pl->num_palettes);

  /* initialize each palette */
  for (ii = 0; ii < pl->num_palettes; ++ii)
  {
    int jj;

    /* get the name and the number of colors */
    pl->palettes[ii].num_colors = palettedata[ii].num_colors;

    /* get all the colors */
    pl->palettes[ii].colors = (rgcolor*)malloc(sizeof(rgcolor) * pl->palettes[ii].num_colors);
    for (jj = 0; jj < pl->palettes[ii].num_colors; ++jj)
    {
      unsigned long color = palettedata[ii].colors[jj];
      pl->palettes[ii].colors[jj].r = ((color & 0xff0000) >> 16) & 0xFF;
      pl->palettes[ii].colors[jj].g = ((color & 0x00ff00) >> 8) & 0xFF;
      pl->palettes[ii].colors[jj].b = ((color & 0x0000FF)) & 0xFF;
    }
  }

  return pl;
}

/**
 * Deletes a list of palettes
 */
void delete_palette_list( palette_list_ptr p )
{
  int ii;
  for (ii = 0; ii < p->num_palettes; ++ii)
  {
    free(p->palettes[ii].colors);
  }

  free(p->palettes);
  free(p);
}

/**
 * Returns the number of palettes in a palette_list
 */
int get_number_of_palettes( palette_list_ptr p )
{
  return p->num_palettes;
}

/**
 * Returns a particular palette in a list of palettes
 */
palette_ptr get_palette( palette_list_ptr p, int idx )
{
  return & p->palettes[idx];
}

/**
 * Converts a double (that should be in the range 0..255) to an unsigned
 * character value.
 */
unsigned char double2char(double d)
{
  if (d > 255) d = 255;
  if (d < 0) d = 0;
  return (unsigned char) d;
}

/**
 * Blends two colors together; the parameter t controls the percentage
 * of the blend.  When t = 0, the first color is returned; when t = 1
 * the seconds color is returned; when t = 0.5 the colors are blended
 * equally.
 */
rgcolor blend(rgcolor startcolor, rgcolor endcolor, double t)
{
  rgcolor mix;

  mix.r = double2char(((double)startcolor.r * (1.0 - t) + (double)endcolor.r * t) + 0.5);
  mix.g = double2char(((double)startcolor.g * (1.0 - t) + (double)endcolor.g * t) + 0.5);
  mix.b = double2char(((double)startcolor.b * (1.0 - t) + (double)endcolor.b * t) + 0.5);

  return mix;
}

/**
 * Makes a color table of 256 colors given a palette_ptr.  This can
 * be used to assign colors to any 256-color image.
 *
 * The color table must be allocated by the user prior to calling this
 * function; it should have size 256*3*2.
 * At return, ctable contains the red/green/blue components for each
 * color in sequence; this takes 256*3 bytes.  These data are then
 * _repeated_ for the second 256*3 bytes (as if there were really 512
 * colors) to make color cycling easier.
 *
 * If randomize is false, then the colors in palette_ptr are used in
 * order to generate the color table.  If randomize is true, then
 * a random number of colors is used, and these are picked randomly
 * from the colors in palette_ptr.
 *
 * If stripes is true, then the color table alternates the colors in
 * the palette_ptr with black.
 */
void get_colortable(
    palette_ptr p,
    unsigned char *ctable,
    int randomize,
    int stripes)
{
  /* first, decide how many bands of color to make */
  int nsteps;
  if (randomize)
  {
    if (stripes)
    {
      /* pick 3 to 5 colors, so that there will be effectively
       6 to 10 bands of color with stripes. */
      nsteps = random() % 3 + 3;
    }
    else
    {
      /* between 5 and 10 colors */
      nsteps = random() % 6 + 5;
    }
  }
  else
  {
    /* just use the colors in the palette_ptr directly */
    nsteps = p->num_colors;
  }

  if (stripes)
    nsteps *= 2;

  /* now, make the list of colors we'll use */
  rgcolor colors[255];
  rgcolor black = {0,0};
  int ii;
  int cindx;

  cindx = 0;
  for (ii = 0; ii < nsteps; ii++)
  {
    if (randomize)
      colors[cindx] = p->colors[random() % p->num_colors];
    else
      colors[cindx] = p->colors[ii];

    ++cindx;

    /* alternate black between the original colors */
    if (stripes)
    {
      colors[cindx] = black;
      ++cindx;
    }
  }

  /* finally, actually fill in the colors in ctable by smoothly
   * blending between the list of colors above */
  for (ii = 0; ii < nsteps; ii++)
  {
    /* now, blend the colors */
    rgcolor startcolor = colors[ii];
    rgcolor endcolor = colors[(ii + 1) % nsteps];
    rgcolor mixcolor;
    int idx;

    for (idx = ii* 256/nsteps; idx < (ii+1)*256/nsteps; idx++)
    {
      double t = nsteps/256.0 * (idx - ii*256/nsteps);

      mixcolor = blend(startcolor, endcolor, t);
      ctable[3*idx + 0] = mixcolor.r;
      ctable[3*idx + 1] = mixcolor.g;
      ctable[3*idx + 2] = mixcolor.b;

      /* make a copy to make cycling easier */
      ctable[3*(idx+256) + 0] = mixcolor.r;
      ctable[3*(idx+256) + 1] = mixcolor.g;
      ctable[3*(idx+256) + 2] = mixcolor.b;
    }
  }
}

/*
#define CAPTIONHEIGHT 14
void drawWindow(GC *pGC, long x, long y, long width, long height, char *pszTitle)
{
    GCSetForegroundColor(pGC, RGB(0xCC, 0xCC, 0xCC));
    GCDrawRectangle(pGC, x, y, width, height);
    GCSetBackgroundColor(pGC, RGB(0xFF, 0xFF, 0xFF));
    GCSetFillMode(pGC, GCOPAQUE|GCOPAQUEFG);
    GCDrawRectangle(pGC, x, y+CAPTIONHEIGHT, width, height-CAPTIONHEIGHT);
    GCDrawHGradientRectangle(pGC, x+1, y+1, width - 2, CAPTIONHEIGHT-1, RGB(0, 0, 0), RGB(0, 0xFF, 0xFF));
    GCSetFillMode(pGC, GCTRANSPARENT|GCOPAQUEFG);
    GCSetForegroundColor(pGC, RGB(0xFF, 0xFF, 0xFF));
    GCDrawText(pGC, pszTitle, x+5, y+4, x+width);
    GCSetBackgroundColor(pGC, RGB(0x0, 0x0, 0x0));
}
*/
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

typedef struct tagGPOINT
{
    long x;
    long y;
    long xdelta;
    long ydelta;
}GPOINT, *PGPOINT;
#define NUM_VECTORS 3
GPOINT gVectors[NUM_VECTORS];

void drawVectors(GC *pGC)
{
    unsigned long t;

    static int iPalOffset;
    int r, g, b;
    r = ((unsigned char*)&colortable_+3*(iPalOffset % 256))[0];
    g = ((unsigned char*)&colortable_+3*(iPalOffset % 256))[1];
    b = ((unsigned char*)&colortable_+3*(iPalOffset % 256))[2];
    GCSetForegroundColor(pGC, (r << 16 | g << 8 | b));
    GCSetFont(pGC, GetFont8x8());
    for (int i = 0; i < NUM_VECTORS; i++)
    {
        GCDrawLine(pGC, gVectors[i].x, gVectors[i].y, gVectors[(i == NUM_VECTORS - 1) ? 0 : i + 1].x, gVectors[(i == NUM_VECTORS - 1) ? 0 : i + 1].y);
    }
    iPalOffset++;
    //GCDimBuffer(pGC);
    for (int i = 0; i < NUM_VECTORS; i++)
    {
        gVectors[i].x += gVectors[i].xdelta;
        if ((gVectors[i].x < 0) || (gVectors[i].x > GCWidth(pGC) - 1)) {
            gVectors[i].xdelta = gVectors[i].xdelta * -1;
            gVectors[i].x += gVectors[i].xdelta;
            gVectors[i].x += gVectors[i].xdelta;
        }
        gVectors[i].y += gVectors[i].ydelta;
        if ((gVectors[i].y < 0) || (gVectors[i].y > GCHeight(pGC) - 1)) {
            gVectors[i].ydelta = gVectors[i].ydelta * -1;
            gVectors[i].y += gVectors[i].ydelta;
            gVectors[i].y += gVectors[i].ydelta;
        }
        if (((rand()&0xff)>0xf0) && ((t=(rand()&0x10)) != 0)) gVectors[i].xdelta = t-0x9;
        if (((rand()&0xff)>0xf0) && ((t=(rand()&0x10)) != 0)) gVectors[i].ydelta = t-0x9;
    }
}

void setupVectors(GC *pGC)
{
    long width, height;
    width = GCWidth(pGC);
    height = GCHeight(pGC);
    GCFastFill(pGC, 0, 0, width, height, 0);
    paletteList_ = init_palette_list();
    get_colortable(get_palette(paletteList_, random() % get_number_of_palettes(paletteList_)),
        colortable_,
        (random() % 2) - 1,
        0);

    for (int i = 0; i < NUM_VECTORS; i++)
    {
        gVectors[i].x = rand() % GCWidth(pGC) - 1;
        gVectors[i].y = rand() % GCHeight(pGC) - 1;
        gVectors[i].xdelta = (rand() & 0x03) - 2;
        gVectors[i].ydelta = (rand() & 0x03) - 2;
    }
}

void GCDrawGlyphs(GC* pGC)
{
    int iPalOffset;
    GCSetOffset(pGC, (GCWidth(pGC) / 2) - 64, (GCHeight(pGC) / 2) - 64);
    for (int y = 0; y < 16; y++)
    {
        for (int x = 0; x < 16; x++)
        {
            uint32_t val = y*16+x;
            if (val <= 0xFF)
            {
                int r, g, b;
                r = ((unsigned char*)&colortable_+3*(iPalOffset % 256))[0];
                g = ((unsigned char*)&colortable_+3*(iPalOffset % 256))[1];
                b = ((unsigned char*)&colortable_+3*(iPalOffset % 256))[2];
                GCSetForegroundColor(pGC, (r << 16 | g << 8 | b));

                char sz[] = {val, 0};
                //GCDrawText(pGC, sz, x*8, y*8, GCWidth(pGC));
                GCFastFill(pGC, x*8, y*8, 8, 8, pGC->foregroundColor);
                iPalOffset++;
            }
        }
    }
    GCSetOffset(pGC, 0, 0);
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
//#define PAL6_TO_8(d) (d << 1)
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
GC gc;

void Task_UpdatePalette()
{
    if (gc.bitmap.handle) {
        GC* pGC = &gc;
        uint8_t uiPalOffset = 0;
        uint32_t uiBorderWidth;
        uint32_t uiCellSizeX, uiCellSizeY;
        uint32_t uiBoxSizeHalfX, uiBoxSizeHalfY;
        uiCellSizeX = GCWidth(pGC)/16;
        uiCellSizeX &= ~1;
        uiCellSizeY = GCHeight(pGC)/16;
        uiCellSizeY &= ~1;
        uiBorderWidth = GCHeight(pGC)/100;
        uiBoxSizeHalfX = ((uiCellSizeX*16)>>1);
        uiBoxSizeHalfY = ((uiCellSizeY*16)>>1);

        GCSetOffset(pGC, (GCWidth(pGC) >> 1) - uiBoxSizeHalfX, (GCHeight(pGC) >> 1) - uiBoxSizeHalfY);
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
                    uint32_t xpos, ypos;
                    xpos = x*uiCellSizeX;
                    ypos = y*uiCellSizeY;
                    GCFastFill(pGC, xpos+uiBorderWidth, ypos+uiBorderWidth, uiCellSizeX - uiBorderWidth, uiCellSizeY - uiBorderWidth, syspalette2[uiPalOffset]);
                    drewsomething = true;
                }
                uiPalOffset++;
            }
            GCPEndAccess(pGC);
        }
        dlo_flush_usb(DLO_HANDLE(&pGC->bitmap), true);
        GCSetOffset(pGC, 0, 0);
    }
}

void dlo_device_configured (dlo_dev_t uid)
{
    GC gcFont = {0};
    printf("gc: dlo device configured\n");
    GCDisplayLinkCreate(&gc, uid);
    setupVectors(&gc);
    GCFastFill(&gc, 0, 0, GCWidth(&gc), GCHeight(&gc), 0);
//GCBOOL GCCreateWithPreallocatedMemory(GC *pGCSource, long width, long height, GCCOLOR *prgBits, GC *pGC);
    //GCCreateWithPreallocatedMemory(&gc, 320, 112, (GCCOLOR*)&image_bits, &gcFont);
    //GCCopyBitsXY(&gc, &gcFont, 0, 0);
    // GCDrawRandomGlyphs(&gc);
    //GCDrawGlyphs(&gc);
    //GCDrawRandomGlyphs(&gc);
    //while(1) {drawVectors(&gc);}
}
