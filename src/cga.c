/* cga.c - CGA framebuffer + BGI primitives.  See cga.h. */
#include <string.h>
#include "cga.h"

unsigned char cga_vram[CGA_VRAM_SIZE];

/* BGI mode CGAC3 = palette 1, low intensity */
const unsigned char cga_palette[4][3] = {
    {   0,   0,   0 },   /* black        */
    {   0, 170, 170 },   /* cyan         */
    { 170,   0, 170 },   /* magenta      */
    { 170, 170, 170 },   /* light gray   */
};

static int cur_color  = 3;   /* BGI current drawing colour (masked to 2 bits) */
static int fill_color = 0;   /* SOLID_FILL colour                             */

void cga_clear(void) { memset(cga_vram, 0, sizeof cga_vram); }

void cga_putpixel(int x, int y, int c)
{
    unsigned off;
    int sh;
    if ((unsigned)x >= CGA_W || (unsigned)y >= CGA_H) return;
    off = (unsigned)((y & 1) ? CGA_BANK : 0) + (unsigned)(y >> 1) * CGA_ROWBYTES + (unsigned)(x >> 2);
    sh  = 6 - 2 * (x & 3);
    cga_vram[off] = (unsigned char)((cga_vram[off] & ~(3 << sh)) | ((c & 3) << sh));
}

int cga_getpixel(int x, int y)
{
    unsigned off;
    if ((unsigned)x >= CGA_W || (unsigned)y >= CGA_H) return 0;
    off = (unsigned)((y & 1) ? CGA_BANK : 0) + (unsigned)(y >> 1) * CGA_ROWBYTES + (unsigned)(x >> 2);
    return (cga_vram[off] >> (6 - 2 * (x & 3))) & 3;
}

/* ------------------------------------------------------------------ */
/* Sprite blitters.                                                    */
/*                                                                     */
/* Faithful ports of av.exe FUN_1000_22d0 / _234e / _23e2.  The image  */
/* is a Borland BGI image buffer: word width-1, word height-1, then    */
/* packed 2bpp rows of (width+3)/4 bytes.  x is truncated to a multiple */
/* of 4 - the caller picks one of four pre-shifted copies to get the   */
/* two low bits.  Offsets wrap in 16 bits like the original's DI.      */
/* ------------------------------------------------------------------ */
#define IMG_W1(p) ((unsigned)((p)[0] | ((p)[1] << 8)))
#define IMG_H1(p) ((unsigned)((p)[2] | ((p)[3] << 8)))

typedef enum { BLIT_COPY, BLIT_XOR, BLIT_ZERO } blit_op;

static void blit(unsigned x, unsigned y, const unsigned char *img, blit_op op)
{
    unsigned short off;
    unsigned nb, rows, i, r;
    const unsigned char *src = img + 4;

    off  = (unsigned short)((x >> 2) + (unsigned short)((y & 0xfffe) * 0x28));
    nb   = (IMG_W1(img) + 4) >> 2;
    rows = (((IMG_H1(img) & 0xff) + 1) >> 1);     /* row pairs */
    if (!nb || !rows) return;

    if ((y & 1) == 0) {
        for (r = 0; r < rows; r++) {
            for (i = 0; i < nb; i++) {
                unsigned char v = (op == BLIT_ZERO) ? 0 : *src++;
                if (op == BLIT_XOR) cga_vram[off & 0x3fff] ^= v;
                else                cga_vram[off & 0x3fff]  = v;
                off++;
            }
            off = (unsigned short)((off - nb) ^ CGA_BANK);
            for (i = 0; i < nb; i++) {
                unsigned char v = (op == BLIT_ZERO) ? 0 : *src++;
                if (op == BLIT_XOR) cga_vram[off & 0x3fff] ^= v;
                else                cga_vram[off & 0x3fff]  = v;
                off++;
            }
            off = (unsigned short)((off ^ CGA_BANK) + (CGA_ROWBYTES - nb));
        }
    } else {
        off = (unsigned short)(off + CGA_BANK);
        for (r = 0; r < rows; r++) {
            for (i = 0; i < nb; i++) {
                unsigned char v = (op == BLIT_ZERO) ? 0 : *src++;
                if (op == BLIT_XOR) cga_vram[off & 0x3fff] ^= v;
                else                cga_vram[off & 0x3fff]  = v;
                off++;
            }
            off = (unsigned short)((off ^ CGA_BANK) + (CGA_ROWBYTES - nb));
            for (i = 0; i < nb; i++) {
                unsigned char v = (op == BLIT_ZERO) ? 0 : *src++;
                if (op == BLIT_XOR) cga_vram[off & 0x3fff] ^= v;
                else                cga_vram[off & 0x3fff]  = v;
                off++;
            }
            off = (unsigned short)((off - nb) ^ CGA_BANK);
        }
    }
}

void cga_put_image(unsigned x, unsigned y, const unsigned char *img) { blit(x, y, img, BLIT_COPY); }
void cga_xor_image(unsigned x, unsigned y, const unsigned char *img) { blit(x, y, img, BLIT_XOR);  }
void cga_clr_image(unsigned x, unsigned y, const unsigned char *img) { blit(x, y, img, BLIT_ZERO); }

/* ------------------------------------------------------------------ */
/* BGI subset                                                          */
/* ------------------------------------------------------------------ */
void bgi_setcolor(int c)     { cur_color  = c & 3; }
void bgi_setfillcolor(int c) { fill_color = c & 3; }

static void hline(int x1, int x2, int y, int c)
{
    int x;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    for (x = x1; x <= x2; x++) cga_putpixel(x, y, c);
}
static void vline(int y1, int y2, int x, int c)
{
    int y;
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    for (y = y1; y <= y2; y++) cga_putpixel(x, y, c);
}

void bgi_rectangle(int x1, int y1, int x2, int y2)
{
    hline(x1, x2, y1, cur_color);
    hline(x1, x2, y2, cur_color);
    vline(y1, y2, x1, cur_color);
    vline(y1, y2, x2, cur_color);
}

void bgi_bar(int x1, int y1, int x2, int y2)
{
    int y;
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    for (y = y1; y <= y2; y++) hline(x1, x2, y, fill_color);
}

/* BGI's circle(x,y,1) in 320x200 CGA comes out as a four pixel diamond;
 * verified against the original running under DOSBox. */
void bgi_circle1(int x, int y)
{
    cga_putpixel(x,     y - 1, cur_color);
    cga_putpixel(x - 1, y,     cur_color);
    cga_putpixel(x + 1, y,     cur_color);
    cga_putpixel(x,     y + 1, cur_color);
}

/* DEFAULT_FONT, LEFT_TEXT/TOP_TEXT, size 1: 8x8 cells, set bits only. */
void bgi_outtextxy(int x, int y, const char *s)
{
    for (; *s; s++, x += 8) {
        const unsigned char *g = bgi_font8x8[(unsigned char)*s];
        int row;
        for (row = 0; row < 8; row++) {
            unsigned char b = g[row];
            int col;
            if (!b) continue;
            for (col = 0; col < 8; col++)
                if (b & (0x80 >> col)) cga_putpixel(x + col, y + row, cur_color);
        }
    }
}

int bgi_imagesize(int x1, int y1, int x2, int y2)
{
    int w = x2 - x1 + 1, h = y2 - y1 + 1;
    return 6 + 2 * ((w + 7) / 8) * h;
}

/* Bytes a w x h image buffer occupies in the form the blitters expect. */
int bgi_image_bytes(int w, int h)
{
    return 4 + ((w + 3) >> 2) * h;
}

/* Reproduces what the original gets from setfillstyle(SOLID,15)+bar+getimage:
 * a w x h image whose first w pixels of each row are `color`, the padding
 * pixels of the last byte left black.  Writes into the caller's buffer;
 * returns 0 if `cap` is too small. */
int bgi_make_solid_image(unsigned char *img, int cap, int w, int h, int color)
{
    int nb = (w + 3) >> 2, x, y;
    if (!img || cap < bgi_image_bytes(w, h)) return 0;
    memset(img, 0, (size_t)bgi_image_bytes(w, h));
    img[0] = (unsigned char)((w - 1) & 0xff); img[1] = (unsigned char)((w - 1) >> 8);
    img[2] = (unsigned char)((h - 1) & 0xff); img[3] = (unsigned char)((h - 1) >> 8);
    for (y = 0; y < h; y++) {
        unsigned char *row = img + 4 + y * nb;
        for (x = 0; x < w; x++) row[x >> 2] |= (unsigned char)((color & 3) << (6 - 2 * (x & 3)));
    }
    return 1;
}
