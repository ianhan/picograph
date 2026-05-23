typedef unsigned long GCCOLOR;
#define GETB(b) ((b) & 0xFF)
#define GETG(g) ((g >> 8) & 0xFF)
#define GETR(r) ((r >> 16) & 0xFF)
#define GETA(a) ((a >> 24) & 0xFF)
#define BMASK 0x000000FF
#define GMASK 0x0000FF00
#define RMASK 0x00FF0000
#define AMASK 0xFF000000
#define RGB(r, g, b)     ((b & BMASK) | ((g << 8) & GMASK) | ((r << 16) & RMASK))
#define RGBA(r, g, b, a) ((b & BMASK) | ((g << 8) & GMASK) | ((r << 16) & RMASK) | ((a << 24) & AMASK))

#ifdef _GC_
GCCOLOR GCColorFrom888(unsigned long color)
{
    return ((color & 0xFF) << 16) |
           (color & 0xFF00) |
           ((color & 0xFF0000) >> 16);
}

unsigned long GCColorTo888(GCCOLOR color)
{
    return ((color & 0xFF) << 16) |
           (color & 0xFF00) |
           ((color & 0xFF0000) >> 16);
}
#endif