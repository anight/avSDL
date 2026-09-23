/* game.c - Arcade Volleyball (DOS, Turbo C + BGI/CGA) reimplemented in
 * portable C.  Every routine below is a transliteration of the matching
 * function in the original AV.EXE; the address in each comment is the
 * offset inside the loaded code segment of the original binary.
 *
 * All arithmetic is kept in the original's 16-bit integer domain where it
 * matters (the RNG in particular), and all drawing goes through the CGA
 * blitters so the output is bit-identical to the DOS version.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cga.h"
#include "plat.h"
#include "avdat.h"

/* ------------------------------------------------------------------ */
/* Data tables that lived in the original's DGROUP                      */
/* ------------------------------------------------------------------ */

/* DS:0x264 - vertical delta for each of the 38 frames of a jump.
 * Sums to zero, so a jump lands exactly where it took off. */
static const short jump_dy[38] = {
    -4,-4,-3,-3,-3,-3,-2,-2,-2,-2,-2,-1,-1,-1,-1,-1,-1,
     0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4
};

/* DS:0x250 - horizontal profile of the rounded top of the net */
static const short net_top[10] = { 0,1,2,3,3,4,6,7,9,14 };

/* DS:0x9E - XT scan codes: {left, right, jump} per player */
static int keys[2][3] = { { 0x2c, 0x2e, 0x2d },    /* Z  C  X            */
                          { 0x4f, 0x51, 0x50 } };  /* KP1 KP3 KP2        */

/* DS:0xAA */
typedef struct { int cur; int count; char text[4][14]; } Menu;
static Menu menus[7] = {
    { 0, 0, { "    Play    ", "",             "",             ""             } },
    { 0, 4, { "PL1 Keyboard", "PL1 Joystick", " PL1  Mouse ", "PL1 Computer" } },
    { 0, 4, { "PL2 Keyboard", "PL2 Joystick", " PL2  Mouse ", "PL2 Computer" } },
    { 0, 2, { "  Sound On  ", " Sound  Off ", "",             ""             } },
    { 0, 0, { "Define  Keys", "",             "",             ""             } },
    { 0, 0, { "Set Joystick", "",             "",             ""             } },
    { 0, 0, { "    Exit    ", "",             "",             ""             } },
};

/* ------------------------------------------------------------------ */
/* State (the original's DGROUP variables; offsets in comments)         */
/* ------------------------------------------------------------------ */
static unsigned char *ball_s[4][4];   /* 0x966 28x22 ball [frame][xshift] */
static int   ball_anim;               /* 0x986 frames until next ball spin */
static int   ball_vx;                 /* 0x988 1/64 px per frame           */
static int   ball_y;                  /* 0x98a on-screen y                 */
static int   px[2];                   /* 0x98c                             */
static int   lastx[2];                /* 0x990 x at last walk-frame flip   */
static unsigned char *p0spr[4][4];    /* 0x994 left player  [frame][shift] */
static unsigned char *net_img;        /* 0x9b4 4x98                        */
static int   ball_vy;                 /* 0x9b6                             */
static int   ai_serve_step;           /* 0x9b8                             */
static unsigned char *line_img;       /* 0x9ba 40x2 court line patch       */
static int   first_hit;               /* 0x9bc serve gets a much harder hit*/
static int   pframe[2];               /* 0x9be                             */
static unsigned char *blank_img;      /* 0x9c2 113x8 solid bar             */
static int   ball_y_prev;             /* 0x9c4                             */
static int   ball_x;                  /* 0x9c6                             */
static int   keystate[2][3];          /* 0x9c8 {left=-2, right=+2, jump=1} */
static int   quit_flag;               /* 0x9d4                             */
static int   side;                    /* 0x9d6 last player to touch / demo */
static int   serve_side;              /* 0x9d8                             */
static unsigned char *edge_l;         /* 0x9da 4x32 left border patch      */
static int   hits;                    /* 0x9dc consecutive touches         */
static int   touching[2];             /* 0x9de                             */
static int   rng;                     /* 0x9e2 16-bit LCG                  */
static int   py[2];                   /* 0x9e4                             */
static int   jumpidx[2];              /* 0x9e8 -1 grounded, -2 just landed */
static int   ball_yf;                 /* 0x9ec 1/64 px                     */
static unsigned char *p1spr[4][4];    /* 0x9ee right player                */
static int   ball_overlap;            /* 0xa0e ball drawn xor'd last frame */
static int   score[2];                /* 0xa10                             */
static int   server;                  /* 0xa14                             */
static unsigned char *ball_b[4][4];   /* 0xa18 38x32 ball (self-erasing)   */
static unsigned char *edge_r;         /* 0xa38 4x32 right border patch     */
static int   ball_x_prev;             /* 0xa3a                             */
static int   ball_bframe;             /* 0xa3c                             */
static int   ai_serve_pat;            /* 0xa3e                             */
static int   ball_xf;                 /* 0xa40 1/64 px                     */
static int   joy_max;                 /* 0xa42                             */
static int   joy_center;              /* 0xa44                             */
static int   serve_pending;           /* 0xa4b ball frozen until first hit */
static int   ball_min_y;              /* 0xa4d highest point of this rally */
static int   sound_on;                /* 0x24e                             */

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
static int iabs(int v) { return v < 0 ? -v : v; }

/* The original called Turbo C's itoa(); keep it local so the score display
 * needs no formatting library. */
static void int_to_str(int v, char *out)
{
    char tmp[8];
    int n = 0;
    if (v < 0) { *out++ = '-'; v = -v; }
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < (int)sizeof tmp);
    while (n) *out++ = tmp[--n];
    *out = '\0';
}
static void rng_step(void)  { rng = (short)(rng * 5 + 1); }   /* in play  */
static void rng_tick(void)  { rng = (short)(rng + 1); }       /* in menu  */

static void wait_retrace(void) { plat_wait_retrace(); }       /* 0x22bf */

/* ------------------------------------------------------------------ */
/* INT 9 handler - 0xc5d                                               */
/* ------------------------------------------------------------------ */
void av_kbd_isr(unsigned char sc)
{
    int i;
    if (sc == 0x81) quit_flag = 1;                /* Esc released -> quit */
    for (i = 0; i < 2; i++) {
        if (menus[i + 1].cur != 0) continue;      /* not on "Keyboard"    */
        if (sc == keys[i][0])          keystate[i][0] = -2;
        if (sc == keys[i][0] + 0x80)   keystate[i][0] = 0;
        if (sc == keys[i][1])          keystate[i][1] = 2;
        if (sc == keys[i][1] + 0x80)   keystate[i][1] = 0;
        if (sc == keys[i][2])          keystate[i][2] = 1;
        if (sc == keys[i][2] + 0x80)   keystate[i][2] = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Sprite loading - 0x41a / 0x345 / 0x43a                              */
/* ------------------------------------------------------------------ */

/* 0x345: build the three horizontally pre-shifted copies of an image so
 * that putimage can work in whole bytes for any x.  The original malloc'd
 * them; here the caller hands over a fixed slab of `each` bytes per copy. */
static int make_shifts(unsigned char **slot, unsigned char *pool, int each)
{
    int w1 = slot[0][0] | (slot[0][1] << 8);
    int h1 = slot[0][2] | (slot[0][3] << 8);
    int nb = (w1 + 4) >> 2;
    int rows = h1 + 1;
    int k;
    if (4 + nb * rows > each) return 0;
    for (k = 0; k < 3; k++) {
        const unsigned char *src = slot[k];
        unsigned char *d = pool + k * each;
        int y, x;
        slot[k + 1] = d;
        for (x = 0; x < 4; x++) *d++ = *src++;
        for (y = 0; y < rows; y++) {
            unsigned char carry = 0;
            for (x = 0; x < nb; x++) {
                unsigned char b = *src++;
                *d++ = (unsigned char)((b >> 2) + carry);
                carry = (unsigned char)(b << 6);
            }
        }
    }
    return 1;
}

/* Record sizes as read by the original's twenty read() calls, and the sprite
 * dimensions each record must declare.  The game needs no data file: AV.DAT is
 * compiled in (src/avdat.c).  A copy on disk is honoured as an override, but
 * only after it has been checked against this table - a short or malformed
 * file would otherwise make the blitters read past the end of a record. */
static const int rec_size[20] = {
    70, 70, 26, 266, 266, 306, 306, 266, 266, 306,
    306, 202, 326, 326, 326, 326, 182, 182, 182, 182
};
static const int rec_w[20] = {
     4,  4, 40,  37,  37,  37,  37,  37,  37,  37,
    37,  4, 38, 38, 38, 38, 28, 28, 28, 28
};
static const int rec_h[20] = {
    32, 32,  2,  26,  26,  30,  30,  26,  26,  30,
    30, 98, 32, 32, 32, 32, 22, 22, 22, 22
};

/* Does `data` hold the twenty records the game expects? */
static int avdat_valid(const unsigned char *data)
{
    int i, off = 0;
    for (i = 0; i < 20; i++) {
        int w1 = data[off] | (data[off + 1] << 8);
        int h1 = data[off + 2] | (data[off + 3] << 8);
        if (w1 != rec_w[i] - 1 || h1 != rec_h[i] - 1) return 0;
        off += rec_size[i];
    }
    return 1;
}

/* 0x43a */
/* Every buffer the game needs, sized at compile time.  Nothing here is
 * allocated at run time - see the sizes derived in the comments. */
static unsigned char avdat[AV_DAT_SIZE];              /* 4688 - the 20 records */

#define SHIFT_PLAYER 304      /* 4 + ((37+3)/4)*30 */
#define SHIFT_BALL_B 324      /* 4 + ((38+3)/4)*32 */
#define SHIFT_BALL_S 158      /* 4 + ((28+3)/4)*22 */
#define BLANK_BYTES  236      /* 4 + ((113+3)/4)*8  */

static unsigned char p0_shift[4][3 * SHIFT_PLAYER];   /* 3648 */
static unsigned char p1_shift[4][3 * SHIFT_PLAYER];   /* 3648 */
static unsigned char bb_shift[4][3 * SHIFT_BALL_B];   /* 3888 */
static unsigned char bs_shift[4][3 * SHIFT_BALL_S];   /* 1896 */
static unsigned char blank_buf[BLANK_BYTES];          /*  236 */

static int load_sprites(void)
{
    unsigned char *rec[20];
    int i, off;
    FILE *f;

    memcpy(avdat, av_dat_builtin, sizeof avdat);

    /* An av.dat in the working directory overrides the built-in copy, the way
     * the original loaded it.  DOS filenames are upper case, so accept both.
     * A file that is not exactly the expected sprite set is rejected rather
     * than trusted - the blitters take their sizes from its headers. */
    f = fopen("av.dat", "rb");
    if (!f) f = fopen("AV.DAT", "rb");
    if (f) {
        size_t n = fread(avdat, 1, sizeof avdat, f);
        fclose(f);
        if (n != sizeof avdat || !avdat_valid(avdat)) {
            fprintf(stderr, "warning: av.dat is not the expected %d-byte sprite "
                            "file; using the built-in copy\n", AV_DAT_SIZE);
            memcpy(avdat, av_dat_builtin, sizeof avdat);
        }
    }

    /* The twenty records are used in place; only the pre-shifted copies need
     * room of their own. */
    for (i = 0, off = 0; i < 20; i++) { rec[i] = avdat + off; off += rec_size[i]; }

    edge_l   = rec[0];
    edge_r   = rec[1];
    line_img = rec[2];
    p1spr[0][0] = rec[3];  p1spr[1][0] = rec[4];
    p1spr[2][0] = rec[5];  p1spr[3][0] = rec[6];
    p0spr[0][0] = rec[7];  p0spr[1][0] = rec[8];
    p0spr[2][0] = rec[9];  p0spr[3][0] = rec[10];
    net_img = rec[11];
    ball_b[0][0] = rec[12]; ball_b[1][0] = rec[13];
    ball_b[2][0] = rec[14]; ball_b[3][0] = rec[15];
    ball_s[0][0] = rec[16]; ball_s[1][0] = rec[17];
    ball_s[2][0] = rec[18]; ball_s[3][0] = rec[19];

    for (i = 0; i < 4; i++) {
        if (!make_shifts(&p0spr[i][0], p0_shift[i], SHIFT_PLAYER) ||
            !make_shifts(&p1spr[i][0], p1_shift[i], SHIFT_PLAYER) ||
            !make_shifts(&ball_b[i][0], bb_shift[i], SHIFT_BALL_B) ||
            !make_shifts(&ball_s[i][0], bs_shift[i], SHIFT_BALL_S))
            return 0;
    }

    /* the original grabs this from the screen after a white bar() */
    blank_img = blank_buf;
    if (!bgi_make_solid_image(blank_buf, (int)sizeof blank_buf, 113, 8, 3)) return 0;
    bgi_setfillcolor(0);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Court - 0xbee                                                       */
/* ------------------------------------------------------------------ */
static void draw_court(void)
{
    wait_retrace();
    cga_clear();
    bgi_rectangle(3, 11, 312, 199);
    cga_put_image(0x9e, 0x67, net_img);
    bgi_outtextxy(0x5a, 0, "Arcade Volleyball");
    bgi_outtextxy(0x28, 0, "0");
    bgi_outtextxy(0x10e, 0, "0");
}

/* ------------------------------------------------------------------ */
/* Player physics - 0xe7a                                              */
/* ------------------------------------------------------------------ */
static void update_players(void)
{
    int p;
    for (p = 0; p < 2; p++) {
        int d, nx, base;
        if (keystate[p][2] != 0 && jumpidx[p] == -1) jumpidx[p] = 0;
        d    = keystate[p][0] + keystate[p][1];
        nx   = d + px[p];
        base = p * 0x9b;
        if (d < 1)                    px[p] = (nx > base + 3)    ? nx : base + 3;
        else                          px[p] = (nx < base + 0x7a) ? nx : base + 0x7a;

        if (jumpidx[p] == -2) { py[p] = 0xad; pframe[p] = 0; jumpidx[p] = -1; }

        if (jumpidx[p] == -1) {
            if (d == 0) pframe[p] = 0;
            else if (iabs(lastx[p] - px[p]) > 4) { pframe[p] ^= 1; lastx[p] = px[p]; }
        } else {
            int i;
            pframe[p] = (jumpidx[p] > 0x12) + 2;
            if (jumpidx[p] == 0x13) py[p] -= 4;      /* stretch at the apex */
            i = jumpidx[p];
            jumpidx[p]++;
            py[p] += jump_dy[i];
            if (jumpidx[p] > 0x25) jumpidx[p] = -2;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Ball physics - 0x1006.  Returns 0 once the ball has hit the floor.  */
/* ------------------------------------------------------------------ */
static int ball_update(void)
{
    int vx = ball_vx, vy = ball_vy, yf;
    if (vx >  0x13f) vx =  0x13f;
    if (vx < -0x13f) vx = -0x13f;
    if (vy >  0x13f) vy =  0x13f;
    if (vy < -0x13f) vy = -0x13f;

    ball_x_prev = ball_x;
    ball_y_prev = ball_y;
    ball_xf += vx;
    ball_yf += vy;

    if (ball_xf < 0x140) {                       /* left wall  */
        ball_xf = 0x140;
        vx = -vx - ((-vx) >> 4);
        vy =  vy - (  vy  >> 4);
        if (side == 1) { side = 2; hits = 0; }
    }
    if (ball_xf > 0x46c0) {                      /* right wall */
        ball_xf = 0x46c0;
        vx = -vx - ((-vx) >> 4);
        vy =  vy - (  vy  >> 4);
        if (side == 0) { side = 2; hits = 0; }
    }
    if (ball_yf < 0x340) {                       /* ceiling    */
        ball_yf = 0x340;
        vx =  vx - (  vx  >> 4);
        vy = -vy - ((-vy) >> 4);
    }
    yf = ball_yf;
    if (yf >= 0x2c81) { ball_yf = 0x2c80; vy = -vy; }   /* floor */

    ball_x  = ball_xf >> 6;
    ball_y  = ball_yf >> 6;
    ball_vx = vx;
    ball_vy = vy + 1;                            /* gravity */
    return yf < 0x2c81;
}

/* ------------------------------------------------------------------ */
/* Collisions - 0x1121                                                 */
/* ------------------------------------------------------------------ */
static void collide(void)
{
    int p, ok;
    for (p = 0; p < 2; p++) {
        int dx = (ball_x - px[p]) - p * 7;
        int dy = (ball_y - py[p]) >> 1;
        if ((dx >> 2) * dx + dy * dy < 0x6e) {       /* inside the blob */
            int r = 8 - (rng & 0xf);                 /* a little chaos  */
            if (jumpidx[p] < 0)
                ball_vy = -iabs(ball_vy);
            else
                ball_vy = (jump_dy[jumpidx[p]] << (3 << first_hit)) - iabs(ball_vy);
            ball_vy += r;
            ball_vx += iabs(dx) * dx
                     + ((keystate[p][1] + keystate[p][0]) << (first_hit + 4))
                     + r;
            if (touching[p] == 0) {
                ball_min_y = 200;
                serve_pending = 0;
                touching[p] = 1;
                if (side == p) hits++;
                else { side = p; hits = 0; }
            }
        } else if (touching[p] != 0) {
            first_hit = 0;
            touching[p] = 0;
        }
    }

    ok = 1;
    if (ball_y > 0x5b) {                              /* below the net top */
        if (ball_x_prev < 0x80 && ball_x > 0x7f) {
            ball_vx = (-iabs(ball_vx)) >> 1;
            ball_xf = 0x1fc0;
            ok = 0;
        } else if (ball_x_prev >= 0xa0 && ball_x < 0xa0) {
            ball_vx = iabs(ball_vx) >> 1;
            ball_xf = 0x2800;
            ok = 0;
        }
    }
    if (ok && ball_y > 0x51 && ball_x > 0x7f && ball_x < 0xa0) {
        if (ball_y < 0x5c) {                          /* the rounded top   */
            if ((ball_x > 0x93 && net_top[0x5b - ball_y] <= 0xa1 - ball_x) ||
                (ball_x < 0x94 && net_top[0x5b - ball_y] <= ball_x - 0x85)) {
                if (ball_vy > 0) {
                    if (ball_x - 0x91 < -5) ball_vx = -iabs(ball_vx);
                    if (ball_x - 0x91 >  5) ball_vx =  iabs(ball_vx);
                    ball_vy = -iabs(ball_vy);
                }
                if (iabs(ball_vx) > 0x20) ball_vx >>= 1;
                if (iabs(ball_vy) > 0x20) ball_vy >>= 1;
            }
        } else if (ball_x < 0x94) ball_vx = -iabs(ball_vx);
        else                      ball_vx =  iabs(ball_vx);
    }
}

/* ------------------------------------------------------------------ */
/* Redraw the court edges the ball has just scribbled over - 0x13ca    */
/* ------------------------------------------------------------------ */
static void repair_borders(int x, int y)
{
    int cx = x - 4, cy = y - 5;
    if (cx < 4)     cx = 4;
    if (cx > 0x110) cx = 0x110;
    if (cy < 0xb)   cy = 0xb;
    if (cy > 0xa7)  cy = 0xa7;
    if (x < 8)      cga_put_image(0,     cy, edge_l);
    if (x > 0x117)  cga_put_image(0x138, cy, edge_r);
    if (y < 0x11)   cga_put_image(cx,  0xb, line_img);
    if (y > 0xab)   cga_put_image(cx,  199, line_img);
}

/* ------------------------------------------------------------------ */
/* Draw one frame - 0x1452                                             */
/* ------------------------------------------------------------------ */
static void draw_frame(void)
{
    int x1 = px[0], x2 = px[1];
    int ov = 0;

    /* The ball is normally drawn with the 38x32 sprite, whose blank margin
     * erases its own previous position.  Near a player (or the floor) that
     * would punch a hole, so it is xor'd with the tight 28x22 sprite. */
    if (iabs(ball_x - x1) < 0x29 && iabs(ball_y - py[0]) <= 0x20) ov = 1;
    else if (iabs(ball_x - x2) < 0x29 && iabs(ball_y - py[1]) <= 0x20) ov = 1;
    else ov = (ball_y_prev < 0xac) ? 0 : 1;

    wait_retrace();

    if (ball_overlap == 0) {
        if (ov) {
            cga_clr_image((unsigned)(ball_x_prev - 4), (unsigned)(ball_y_prev - 5),
                          ball_b[ball_bframe][(ball_x_prev - 4) & 3]);
            repair_borders(ball_x_prev, ball_y_prev);
        }
    } else {
        cga_clr_image((unsigned)ball_x_prev, (unsigned)ball_y_prev,
                      ball_s[ball_bframe][ball_x_prev & 3]);
    }
    ball_overlap = ov;

    if (ball_anim-- == 0) {                       /* spin speed = ball speed */
        int n = 0xc - ((iabs(ball_vx) + iabs(ball_vy)) >> 6);
        ball_anim = iabs(n);
        ball_bframe = (ball_bframe + 1) & 3;
    }

    cga_put_image((unsigned)x1, (unsigned)py[0], p0spr[pframe[0]][x1 & 3]);
    cga_put_image((unsigned)x2, (unsigned)py[1], p1spr[pframe[1]][x2 & 3]);

    if (ball_overlap == 0)
        cga_put_image((unsigned)(ball_x - 4), (unsigned)(ball_y - 5),
                      ball_b[ball_bframe][(ball_x - 4) & 3]);
    else
        cga_xor_image((unsigned)ball_x, (unsigned)ball_y,
                      ball_s[ball_bframe][ball_x & 3]);

    repair_borders(ball_x, ball_y);
    if (x1 < 6)     cga_put_image(0,     (unsigned)(py[0] - 2), edge_l);
    if (x2 > 0x113) cga_put_image(0x138, (unsigned)(py[1] - 2), edge_r);
    cga_put_image(0x9e, 0x67, net_img);
}

/* Erase the ball and repaint the static scene - 0x166f */
static void redraw_static(void)
{
    wait_retrace();
    if (ball_overlap == 0)
        cga_clr_image((unsigned)(ball_x - 4), (unsigned)(ball_y - 5),
                      ball_b[ball_bframe][(ball_x - 4) & 3]);
    else
        cga_clr_image((unsigned)ball_x, (unsigned)ball_y,
                      ball_s[ball_bframe][ball_x & 3]);
    cga_put_image((unsigned)px[0], (unsigned)py[0], p0spr[pframe[0]][px[0] & 3]);
    cga_put_image((unsigned)px[1], (unsigned)py[1], p1spr[pframe[1]][px[1] & 3]);
    bgi_rectangle(3, 11, 312, 199);
    cga_put_image(0x9e, 0x67, net_img);
}

/* ------------------------------------------------------------------ */
/* End of rally - 0x1747                                               */
/* ------------------------------------------------------------------ */
static void point_scored(void)
{
    int winner, maxvx, maxvy, n, t, xb;
    char buf[16];

    if (hits < 3) winner = (ball_x < 0x96) ? 1 : 0;   /* whose floor?      */
    else          winner = 1 - side;                  /* four touches      */

    draw_frame();
    maxvy = iabs(ball_vy) >> 3;
    maxvx = iabs(ball_vx) >> 3;

    n = 0x14;
    do {
        keystate[1][2] = 0;
        keystate[0][2] = 0;
        plat_nosound();
        update_players();
        if (maxvx < iabs(ball_vx)) ball_vx = (ball_vx < 0) ? -maxvx : maxvx;
        if (maxvy < iabs(ball_vy)) ball_vy = (ball_vy < 0) ? -maxvy : maxvy;
        collide();
        if (sound_on) plat_sound(winner == server ? 5000 : 100);
        ball_update();
        draw_frame();
        t = n--;
    } while (t > 0 || jumpidx[0] != -1 || jumpidx[1] != -1);
    plat_nosound();

    xb = winner * 0xe6;
    if (winner == server) {                      /* side out already held  */
        bgi_bar(xb + 0x28, 0, xb + 0x37, 7);
        score[winner]++;
        int_to_str(score[winner], buf);
        bgi_outtextxy(xb + 0x28, 0, buf);
        if (score[winner] > 14 && score[winner] - score[1 - winner] > 1)
            quit_flag = 1;
    } else {                                     /* change of serve        */
        bgi_setcolor(0);    bgi_circle1((1 - winner) * 0xe6 + 0x23, 3);
        bgi_setcolor(0xf);  bgi_circle1(winner * 0xe6 + 0x23, 3);
        server = winner;
    }
    plat_delay(100);
    redraw_static();

    t = winner * 0xa5 + 0x40;
    ball_x_prev = t;  ball_x = t;  ball_xf = t * 0x40;
    ball_y_prev = 0x87; ball_y = 0x87; ball_yf = 0x21c0;
    touching[1] = 0; touching[0] = 0;
    hits = 0; ball_vy = 0; ball_vx = 0;
    ball_overlap = 0; ball_bframe = 0; ball_anim = 6;
    first_hit = 1; serve_pending = 1; side = 2;

    t = iabs(rng);
    ai_serve_pat = t % 5;
    if (score[server] == 14) ai_serve_pat = 5;   /* the match-point serve  */
    ai_serve_step = 0;
}

/* ------------------------------------------------------------------ */
/* Input back ends                                                     */
/* ------------------------------------------------------------------ */

/* 0xd50 - game port joystick.  The original timed the one-shot on port
 * 0x201; we take the same decisions straight from the SDL axis. */
static void read_joystick(int p)
{
    int ax = plat_joystick_xaxis(p);
    keystate[p][2] = plat_joystick_button(p) ? 1 : 0;
    keystate[p][1] = (ax > 0) ?  2 : 0;
    keystate[p][0] = (ax < 0) ? -2 : 0;
}

/* 0xde9 - INT 33h mouse: left button jumps, horizontal motion steers. */
static void read_mouse(int p)
{
    int dx;
    keystate[p][2] = plat_mouse_buttons() & 1;
    dx = plat_mouse_take_dx();
    keystate[p][1] = (dx > 0) ?  2 : 0;
    keystate[p][0] = (dx < 0) ? -2 : 0;
}

/* 0x724 - joystick presence/centre calibration */
static int joy_calibrate(void)
{
    if (!plat_joystick_present()) return 0;
    joy_center = 100;
    joy_max = (joy_center >> 1) + joy_center;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Computer players                                                    */
/* ------------------------------------------------------------------ */

/* 0x199b - walk player p towards `target`; 1 once within `tol`. */
static int ai_move(int p, int target, int tol)
{
    int cur = px[p];
    if (iabs(cur - target) < tol) { keystate[p][0] = 0; keystate[p][1] = 0; return 1; }
    if (cur < target) { keystate[p][0] =  0; keystate[p][1] = 2; }
    else              { keystate[p][0] = -2; keystate[p][1] = 0; }
    return 0;
}

/* 0x19f4 - left-hand computer player */
static void ai_left(void)
{
    int rnd, t, predx, r = 0;

    keystate[0][2] = 0;
    if (ball_y < ball_min_y) ball_min_y = ball_y;
    rnd = 5 - rng % 10;

    if (serve_pending == 0 || (server & 1) != 0) {
        if (ball_vy < 1 || ball_x > 0x8b) { ai_move(0, 0x38, 8); return; }

        t = (ball_vy >> 6) ? (0x8c - ball_y) / (ball_vy >> 6) : 0;
        predx = (t < 1 || (ball_vx >> 6) == 0) ? ball_x
                                               : (ball_vx >> 6) * t + ball_x - 4;

        if (iabs(ball_vx) < 0x80 && ball_min_y < 0x4b) {   /* soft lob: set up */
            if ((ball_y < 0x9e) != (ball_vx < 0)) ai_move(0, ball_x - 0xf, 3);
            else                                  ai_move(0, ball_x + 0xf, 3);
            return;
        }
        if (ball_y < 0x83) {                                /* chase the landing spot */
            if (predx < 3)    predx = 6 - predx;
            if (predx > 0x7b) predx = 0xf6 - predx;
            ai_move(0, predx + rnd, 3);
            return;
        }
        if (iabs(px[0] - ball_x) > 6 && iabs(ball_vx) < 0x400) {
            ai_move(0, ball_x - ((px[0] - 0x3c) >> 3), 3);
            return;
        }
        ai_move(0, ((px[0] - 0x3c) >> 3) + ball_x + rnd, 10);
        keystate[0][2] = (px[0] < 0x69 && (side != 0 || hits < 2)) ? 1 : 0;
        return;
    }

    switch (ai_serve_pat) {                        /* serve wind-ups */
    case 0: r = ai_move(0, 0x37, 2); break;
    case 1: r = ai_move(0, 0x54, 2); break;
    case 2: r = ai_move(0, 0x50, 2); break;
    case 3:
        if (ai_serve_step == 0) { r = ai_move(0, 0x2c, 2); if (r) ai_serve_step = 1; r = 0; }
        else { ai_move(0, 0x3a, 2); r = 1; }
        break;
    case 4:
        if (ai_serve_step == 0) { r = ai_move(0, 0x5a, 2); if (r) ai_serve_step = 1; r = 0; }
        else { ai_move(0, 0x3a, 2); r = 1; }
        break;
    case 5:
        if (ai_serve_step == 0) { if (ai_move(0, 3, 2)) ai_serve_step = 1; r = 0; }
        else { int s = ai_serve_step++; ai_move(0, s + 8, 1); r = 1; }
        break;
    default: r = 0; break;
    }
    keystate[0][2] = r;
}

/* 0x1cea - right-hand computer player (mirror image of the above) */
static void ai_right(void)
{
    int rnd, t, predx, r = 0;

    keystate[1][2] = 0;
    if (ball_y < ball_min_y) ball_min_y = ball_y;
    rnd = 5 - rng % 10;

    if (serve_pending == 0 || (server & 1) != 1) {
        if (ball_vy < 1 || ball_x < 0x7e) { ai_move(1, 0xd3, 8); return; }

        t = (ball_vy >> 6) ? (0x8c - ball_y) / (ball_vy >> 6) : 0;
        predx = (t < 1 || (ball_vx >> 6) == 0) ? ball_x
                                               : (ball_vx >> 6) * t + ball_x - 4;

        if (iabs(ball_vx) < 0x80 && ball_min_y < 0x4b) {
            if ((ball_y < 0x9e) != (ball_vx < 0)) ai_move(1, ball_x + 0xf, 3);
            else                                  ai_move(1, ball_x - 0xf, 3);
            return;
        }
        if (ball_y < 0x83) {
            if (predx < 0x9e)  predx = 0x13c - predx;
            if (predx > 0x115) predx = 0x22a - predx;
            ai_move(1, predx - rnd, 3);
            return;
        }
        if (iabs(px[1] - ball_x) > 6 && iabs(ball_vx) < 0x400) {
            ai_move(1, ((px[1] - 0xda) >> 3) + ball_x, 3);
            return;
        }
        ai_move(1, (ball_x - rnd) - ((px[1] - 0xda) >> 3), 10);
        keystate[1][2] = (px[1] < 0xb0 || (side == 1 && hits > 1)) ? 0 : 1;
        return;
    }

    switch (ai_serve_pat) {
    case 0: r = ai_move(1, 0xe8, 2); break;
    case 1: r = ai_move(1, 0xca, 2); break;
    case 2: r = ai_move(1, 0xd0, 2); break;
    case 3:
        if (ai_serve_step == 0) { r = ai_move(1, 0xfa, 2); if (r) ai_serve_step = 1; r = 0; }
        else { ai_move(1, 0xdc, 2); r = 1; }
        break;
    case 4:
        if (ai_serve_step == 0) { r = ai_move(1, 0xbe, 2); if (r) ai_serve_step = 1; r = 0; }
        else { ai_move(1, 0xe6, 2); r = 1; }
        break;
    case 5:
        if (ai_serve_step == 0) { if (ai_move(1, 0x115, 2)) ai_serve_step = 1; r = 0; }
        else { int s = ai_serve_step++; ai_move(1, 0x110 - s, 1); r = 1; }
        break;
    default: r = 0; break;
    }
    keystate[1][2] = r;
}

/* ------------------------------------------------------------------ */
/* One game - 0x2008                                                   */
/* ------------------------------------------------------------------ */
static void play_round(void)
{
    int i, ctl1 = 0, ctl2 = 0, alt = 0, live = 0;
    char c;

    ball_vy = 0; ball_vx = 0; ball_overlap = 0; ball_bframe = 0;
    hits = 0; ball_anim = 6; ball_min_y = 200;

    if (getenv("AV_DETERMINISTIC")) {   /* used only by the A/B test harness */
        px[0] = 0x40; px[1] = 0xe2; rng = 0;
    }

    for (i = 0; i < 2; i++) {
        lastx[i]   = px[i];
        py[i]      = 0xad;
        jumpidx[i] = -1;
        pframe[i]  = 0;
        keystate[i][2] = 0; keystate[i][0] = 0; keystate[i][1] = 0;
        score[i]    = 0;
        touching[i] = 0;
        /* a human always serves first against the computer */
        if (menus[i + 1].cur < menus[i + 1].count - 1 &&
            menus[2 - i].cur == menus[2 - i].count - 1)
            serve_side = i;
    }
    joy_max = (joy_center >> 1) + joy_center;

    i = serve_side * 0xa5 + 0x40;
    ball_x_prev = i; ball_x = i; ball_xf = i * 0x40;
    ball_y_prev = 0x87; ball_y = 0x87; ball_yf = 0x21c0;
    server = serve_side + 2;        /* neither 0 nor 1: first point flips it */
    side = 2;
    first_hit = 1;
    serve_pending = 1;
    quit_flag = 0;

    c = menus[1].text[menus[1].cur][4];
    if (c == ' ') ctl1 = 2; else if (c == 'C') ctl1 = 3; else if (c == 'J') ctl1 = 1;
    c = menus[2].text[menus[2].cur][4];
    if (c == ' ') ctl2 = 2; else if (c == 'C') ctl2 = 3; else if (c == 'J') ctl2 = 1;

    plat_set_raw_kbd(1);
    if (ctl1 == 2 || ctl2 == 2) plat_set_mouse_grab(1);
    draw_court();

    for (;;) {
        if (quit_flag) break;
        rng_step();
        switch (ctl1) {
        case 1: read_joystick(0); break;
        case 2: if (alt) read_mouse(0); break;
        case 3: ai_left(); break;
        default: break;
        }
        switch (ctl2) {
        case 1: read_joystick(1); break;
        case 2: if (alt) read_mouse(1); break;
        case 3: ai_right(); break;
        default: break;
        }
        alt = 1 - alt;

        update_players();
        if (serve_pending) { collide(); draw_frame(); live = 1; }
        else if (live)     { collide(); live = ball_update(); draw_frame(); }

        if (live && hits <= 2) continue;
        point_scored();
    }

    redraw_static();
    plat_set_mouse_grab(0);
    plat_set_raw_kbd(0);
    while (plat_kbhit()) plat_getch();
}

/* ------------------------------------------------------------------ */
/* Menu - 0x63f / 0x779 / 0x965 / 0x9af                                */
/* ------------------------------------------------------------------ */

/* 0x63f - idle: the two players bounce about behind the menu. */
static int idle_anim(void)
{
    while (!plat_kbhit()) {
        int x1, x2;
        rng_tick();
        if (jumpidx[side] == -1) {
            keystate[side][2] = 0;
            side = 1 - side;
            keystate[side][2] = 1;
        }
        update_players();
        x1 = px[0]; x2 = px[1];
        wait_retrace();
        cga_put_image((unsigned)x1, (unsigned)py[0], p0spr[pframe[0]][x1 & 3]);
        cga_put_image((unsigned)x2, (unsigned)py[1], p1spr[pframe[1]][x2 & 3]);
        if (x1 < 6)     cga_put_image(0,     (unsigned)(py[0] - 2), edge_l);
        if (x2 > 0x113) cga_put_image(0x138, (unsigned)(py[1] - 2), edge_r);
        cga_put_image(0x9e, 0x67, net_img);
    }
    return plat_getch();
}

/* 0x965 - read one key for "Define Keys" and echo it; returns the scan code */
static int read_define_key(int y)
{
    char shown[2];
    int k = plat_bioskey0();
    shown[0] = (char)(k & 0xff);
    shown[1] = 0;
    bgi_outtextxy(200, y, shown);
    return (k >> 8) & 0xff;
}

/* 0x779 - the menu proper.  Returns the index of the entry Enter was
 * pressed on when that entry has no options of its own. */
static int menu_select(void)
{
    int sel = 0, i, ch;

    for (i = 0; i < 7; i++) cga_clr_image(0x68, (unsigned)(i * 8 + 0x28), blank_img);
    wait_retrace();
    for (i = 0; i < 7; i++)
        bgi_outtextxy(0x70, i * 8 + 0x28, menus[i].text[menus[i].cur]);
    cga_xor_image(0x68, 0x28, blank_img);

    for (;;) {
        ch = idle_anim();
        if (ch == '\r') {
            if (menus[sel].count < 1) {
                for (i = 0; i < 7; i++)
                    cga_clr_image(0x68, (unsigned)(i * 8 + 0x28), blank_img);
                return sel;
            }
            menus[sel].cur = (menus[sel].cur + 1) % menus[sel].count;
            cga_clr_image(0x68, (unsigned)(sel * 8 + 0x28), blank_img);
            bgi_outtextxy(0x70, sel * 8 + 0x28, menus[sel].text[menus[sel].cur]);
        } else {
            if (plat_kbhit()) ch = plat_getch();     /* extended key tail */
            plat_sound(4000); plat_nosound();        /* the menu click    */
            wait_retrace();
            cga_xor_image(0x68, (unsigned)(sel * 8 + 0x28), blank_img);
            if (ch == '2' || ch == 'P') sel = (sel + 1) % 7;
            if (ch == '8' || ch == 'H') sel = (sel + 6) % 7;
        }
        cga_xor_image(0x68, (unsigned)(sel * 8 + 0x28), blank_img);
    }
}

/* 0x9af - the menu screen; returns 0 when "Exit" was chosen. */
static int menu_screen(void)
{
    int sel, i;

    cga_clr_image((unsigned)px[0], (unsigned)py[0], p0spr[pframe[0]][px[0] & 3]);
    cga_clr_image((unsigned)px[1], (unsigned)py[1], p1spr[pframe[1]][px[1] & 3]);
    bgi_rectangle(3, 11, 312, 199);
    cga_put_image(0x9e, 0x67, net_img);

    for (i = 0; i < 2; i++) {
        pframe[i] = 0;
        keystate[i][0] = 0; keystate[i][1] = 0; keystate[i][2] = i;
        jumpidx[i] = i - 1;
        py[i] = 0xad;
    }
    side = 1;

    do {
        sel = menu_select();
        if (sel == 4) {
            static const char *lbl[6] = { "PL1 Left  : ", "PL1 Right : ", "PL1 Jump  : ",
                                          "PL2 Left  : ", "PL2 Right : ", "PL2 Jump  : " };
            for (i = 0; i < 6; i++) {
                bgi_outtextxy(0x68, 0x28 + i * 8, lbl[i]);
                keys[i / 3][i % 3] = read_define_key(0x28 + i * 8);
            }
            for (i = 0; i < 7; i++)
                cga_clr_image(0x68, (unsigned)(i * 8 + 0x28), blank_img);
        }
        if (sel == 5) {
            bgi_outtextxy(0x28, 0x28, "Center stick then press a key.");
            plat_getch();
            joy_calibrate();
            for (i = 0; i < 4; i++)
                cga_clr_image((unsigned)(i * 0x2a + 0x28), 0x28, blank_img);
        }
    } while (sel > 0 && sel < 6);

    if (sel != 6) sound_on = menus[3].cur ^ 1;
    plat_set_sound_enabled(sound_on);
    return sel != 6;
}

/* ------------------------------------------------------------------ */
/* main - 0x1a5                                                        */
/* ------------------------------------------------------------------ */
int av_main(void)
{
    int i, j;

    if (!load_sprites()) {
        bgi_outtextxy(0, 0, "File ``AV.DAT'' not found.");
        return 1;
    }
    sound_on   = 0;
    serve_side = 0;
    if (getenv("AV_DETERMINISTIC")) { menus[1].cur = 3; menus[2].cur = 3; }

    if (!plat_mouse_present()) {                 /* drop " PLn  Mouse " */
        for (i = 1; i < 3; i++) {
            for (j = 0; j < 14; j++) menus[i].text[2][j] = menus[i].text[3][j];
            menus[i].count--;
        }
    }
    if (!plat_joystick_present() || !joy_calibrate()) {   /* drop "PLn Joystick" */
        for (i = 1; i < 3; i++) {
            for (j = 0; j < 14; j++) {
                menus[i].text[1][j] = menus[i].text[2][j];
                menus[i].text[2][j] = menus[i].text[3][j];
            }
            menus[i].count--;
        }
    }

    draw_court();
    px[0] = 0x40; px[1] = 0xe2;
    py[0] = 0xad; py[1] = 0xad;

    while (menu_screen()) {
        play_round();
        serve_side ^= 1;
    }
    return 0;
}
