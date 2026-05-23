typedef unsigned long GCCOLOR;
#define GETR(r) ((r) & 0xFF)
#define GETG(g) ((g >> 8) & 0xFF)
#define GETB(b) ((b >> 16) & 0xFF)
#define GETA(a) ((a >> 24) & 0xFF)
#define RMASK 0x000000FF
#define GMASK 0x0000FF00
#define BMASK 0x00FF0000
#define AMASK 0xFF000000
#define RGB(r, g, b)     ((r & RMASK) | ((g << 8) & GMASK) | ((b << 16) & BMASK))
#define RGBA(r, g, b, a) ((r & RMASK) | ((g << 8) & GMASK) | ((b << 16) & BMASK) | ((a << 24) & AMASK))