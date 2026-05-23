typedef unsigned char GCCOLOR;
#define GCCOLOR_ONEBYTE
#define GETB(b) (0)
#define GETG(g) ((g) & 0x0F)
#define GETR(r) ((r >> 4) & 0x0F)
#define GETA(a) (0)
#define BMASK 0x00000000
#define GMASK 0x0000000F
#define RMASK 0x000000F0
#define AMASK 0x00000000
#define RG(r, g)         (((g) & GMASK) | ((r << 4) & RMASK))
#define RGB(r, g, b)     ((b & BMASK) | ((g) & GMASK) | ((r << 4) & RMASK))
#define RGBA(r, g, b, a) ((b & BMASK) | ((g) & GMASK) | ((r << 4) & RMASK) | (a & AMASK))

#ifdef _GC_
GCCOLOR GCColorFrom888(unsigned long color)
{
    return RG((color & 0xFF) / 16, ((color >> 8) & 0xFF) / 16);
}

unsigned long GCColorTo888(GCCOLOR color)
{
    return GETR(color) * 16 | ((GETG(color) * 16) << 8);
}
#endif