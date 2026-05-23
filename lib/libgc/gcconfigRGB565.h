typedef unsigned long GCCOLOR;
#define GETB(b) ((b) & 0x1F)
#define GETG(g) ((g >> 5) & 0x3F)
#define GETR(r) ((r >> 11) & 0x1F)
#define GETA(a) (0)
#define BMASK 0x0000001F
#define GMASK 0x000007E0
#define RMASK 0x0000F800
#define AMASK 0x00000000
#undef RGB
#define RGB(r, g, b)     ((b & BMASK) | ((g << 5) & GMASK) | ((r << 11) & RMASK))
#define RGBA(r, g, b, a) ((b & BMASK) | ((g << 5) & GMASK) | ((r << 11) & RMASK))

#ifdef _GC_
#define GETG24(g) ((g >> 8) & 0xFF)
#define GETR24(r) ((r >> 16) & 0xFF)
#define GETB24(b) ((b) & 0xFF)
GCCOLOR GCColorFrom888(unsigned long color)
{
    int R5 = ( GETR24(color) * 249 + 1014 ) >> 11;
    int G6 = ( GETG24(color) * 253 +  505 ) >> 10;
    int B5 = ( GETB24(color) * 249 + 1014 ) >> 11;
    return RGB(R5, G6, B5);
}

unsigned long GCColorTo888(GCCOLOR color)
{
    int R8 = ( GETR(color) * 527 + 23 ) >> 6;
    int G8 = ( GETG(color) * 259 + 33 ) >> 6;
    int B8 = ( GETB(color) * 527 + 23 ) >> 6;
    return R8 << 11 | G8 << 5 | B8;
}
#endif