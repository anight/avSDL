/* cga.h - CGA 320x200x4 framebuffer emulation plus the handful of Borland
 * BGI primitives the original game uses.
 *
 * The original writes straight into B800:0000 with hand-rolled blitters
 * (FUN_1000_22d0 / _234e / _23e2 in av.exe).  We keep the exact same memory
 * layout - two interleaved banks of 80 bytes/row, 2 bits/pixel, MSB first -
 * so those routines can be transliterated verbatim and produce bit-identical
 * output.
 */
#ifndef AV_CGA_H_INCLUDED
#define AV_CGA_H_INCLUDED

#define CGA_W          320
#define CGA_H          200
#define CGA_VRAM_SIZE  16384
#define CGA_BANK       0x2000
#define CGA_ROWBYTES   80

extern unsigned char cga_vram[CGA_VRAM_SIZE];
extern const unsigned char bgi_font8x8[256][8];

/* CGAC3: palette 1, low intensity -> black / cyan / magenta / light gray */
extern const unsigned char cga_palette[4][3];

void cga_clear(void);
void cga_putpixel(int x, int y, int c);
int  cga_getpixel(int x, int y);

/* --- direct sprite blitters (ports of the original's inline asm) --- */
void cga_put_image(unsigned x, unsigned y, const unsigned char *img);  /* copy */
void cga_xor_image(unsigned x, unsigned y, const unsigned char *img);  /* xor  */
void cga_clr_image(unsigned x, unsigned y, const unsigned char *img);  /* zero */

/* --- BGI subset --- */
void bgi_setcolor(int c);
void bgi_setfillcolor(int c);
void bgi_rectangle(int x1, int y1, int x2, int y2);
void bgi_bar(int x1, int y1, int x2, int y2);
void bgi_circle1(int x, int y);        /* circle(x,y,1) as BGI draws it */
void bgi_outtextxy(int x, int y, const char *s);

/* BGI image helpers.  No allocation anywhere in this layer: the caller owns
 * the buffer and bgi_make_solid_image reports whether it was big enough. */
int  bgi_imagesize(int x1, int y1, int x2, int y2);
int  bgi_image_bytes(int w, int h);
int  bgi_make_solid_image(unsigned char *img, int cap, int w, int h, int color);

#endif
