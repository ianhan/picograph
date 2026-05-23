typedef unsigned long GCCOLOR;
#define GETB(b) ((b) & 0x0F)
#define GETG(g) ((g >> 4) & 0xF)
#define GETR(r) ((r >> 8) & 0xF)
#define GETA(a) ((a >> 12) & 0xF)
#define BMASK 0x0000000F
#define GMASK 0x000000F0
#define RMASK 0x00000F00
#define AMASK 0x0000F000
#define RGB(r, g, b)     ((b & BMASK) | ((g << 4) & GMASK) | ((r << 8) & RMASK))
#define RGBA(r, g, b, a) ((b & BMASK) | ((g << 4) & GMASK) | ((r << 8) & RMASK) | ((a << 12) & AMASK))

#ifdef _GC_
GCCOLOR GCColorFrom888(unsigned long color)
{
    return RGB((color & 0xFF) / 16, ((color >> 8) & 0xFF) / 16, ((color >> 16) & 0xFF) / 16);
}

unsigned long GCColorTo888(GCCOLOR color)
{
    return GETR(color) * 16 | ((GETG(color) * 16) << 8) | ((GETB(color) * 16) << 16);
}
#endif