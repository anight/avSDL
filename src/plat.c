/* plat.c - the platform layer, for desktop SDL2 and for picosdl.
 *
 * The game (game.c) talks only to plat.h, which stands in for the DOS
 * services the original used.  Most of that is the same whichever SDL is
 * underneath - the XT scancode table, the getch() queue, the 60 Hz pacing
 * and the PC-speaker square wave - and is written once, at the top.  What
 * differs is the screen, the pointing devices and the audio format, and that
 * is split on PICOSDL_SDL_H, which picosdl's <SDL2/SDL.h> defines:
 *
 *   desktop  a window, a streaming texture, the mouse, any joystick SDL finds
 *   picosdl  one 320x200 byte-per-pixel canvas handed to the panel, the CGA
 *            palette written into the display's CLUT, the board's stick as
 *            the game port, the I2C pad in the mouse's menu slot, and a
 *            Bluetooth keyboard where the build has one
 *
 * Neither half allocates.
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "plat.h"
#include "cga.h"

#ifndef KMOD_CAPS           /* picosdl tracks neither lock */
#define KMOD_CAPS 0
#endif
#ifndef KMOD_NUM
#define KMOD_NUM  0
#endif

#define PIT_HZ      1193181u
#define FRAME_HZ    60u

void (*plat_frame_hook)(void);

static int  raw_kbd;

/* ================================================================== */
/* Keyboard: XT scan codes and the getch() queue (both builds)         */
/* ================================================================== */

#define QSZ 64
static unsigned char kq[QSZ];
static int kq_head, kq_tail;
static void kq_push(unsigned char c) { int n = (kq_tail + 1) % QSZ; if (n != kq_head) { kq[kq_tail] = c; kq_tail = n; } }
static int  kq_empty(void) { return kq_head == kq_tail; }
static int  kq_pop(void) { int c; if (kq_empty()) return -1; c = kq[kq_head]; kq_head = (kq_head + 1) % QSZ; return c; }

/* bioskey(0) wants the raw (scancode, ascii) pair; -1 is "cancelled" */
static int  bk_have, bk_val;

static unsigned char xt_of_sdl[SDL_NUM_SCANCODES];

static void build_scancode_table(void)
{
    static const struct { int sdl; unsigned char xt; } m[] = {
        {SDL_SCANCODE_ESCAPE,0x01},
        {SDL_SCANCODE_1,0x02},{SDL_SCANCODE_2,0x03},{SDL_SCANCODE_3,0x04},{SDL_SCANCODE_4,0x05},
        {SDL_SCANCODE_5,0x06},{SDL_SCANCODE_6,0x07},{SDL_SCANCODE_7,0x08},{SDL_SCANCODE_8,0x09},
        {SDL_SCANCODE_9,0x0a},{SDL_SCANCODE_0,0x0b},{SDL_SCANCODE_MINUS,0x0c},{SDL_SCANCODE_EQUALS,0x0d},
        {SDL_SCANCODE_BACKSPACE,0x0e},{SDL_SCANCODE_TAB,0x0f},
        {SDL_SCANCODE_Q,0x10},{SDL_SCANCODE_W,0x11},{SDL_SCANCODE_E,0x12},{SDL_SCANCODE_R,0x13},
        {SDL_SCANCODE_T,0x14},{SDL_SCANCODE_Y,0x15},{SDL_SCANCODE_U,0x16},{SDL_SCANCODE_I,0x17},
        {SDL_SCANCODE_O,0x18},{SDL_SCANCODE_P,0x19},{SDL_SCANCODE_LEFTBRACKET,0x1a},
        {SDL_SCANCODE_RIGHTBRACKET,0x1b},{SDL_SCANCODE_RETURN,0x1c},{SDL_SCANCODE_LCTRL,0x1d},
        {SDL_SCANCODE_RCTRL,0x1d},
        {SDL_SCANCODE_A,0x1e},{SDL_SCANCODE_S,0x1f},{SDL_SCANCODE_D,0x20},{SDL_SCANCODE_F,0x21},
        {SDL_SCANCODE_G,0x22},{SDL_SCANCODE_H,0x23},{SDL_SCANCODE_J,0x24},{SDL_SCANCODE_K,0x25},
        {SDL_SCANCODE_L,0x26},{SDL_SCANCODE_SEMICOLON,0x27},{SDL_SCANCODE_APOSTROPHE,0x28},
        {SDL_SCANCODE_GRAVE,0x29},{SDL_SCANCODE_LSHIFT,0x2a},{SDL_SCANCODE_BACKSLASH,0x2b},
        {SDL_SCANCODE_Z,0x2c},{SDL_SCANCODE_X,0x2d},{SDL_SCANCODE_C,0x2e},{SDL_SCANCODE_V,0x2f},
        {SDL_SCANCODE_B,0x30},{SDL_SCANCODE_N,0x31},{SDL_SCANCODE_M,0x32},{SDL_SCANCODE_COMMA,0x33},
        {SDL_SCANCODE_PERIOD,0x34},{SDL_SCANCODE_SLASH,0x35},{SDL_SCANCODE_RSHIFT,0x36},
        {SDL_SCANCODE_KP_MULTIPLY,0x37},{SDL_SCANCODE_LALT,0x38},{SDL_SCANCODE_RALT,0x38},
        {SDL_SCANCODE_SPACE,0x39},{SDL_SCANCODE_CAPSLOCK,0x3a},
        {SDL_SCANCODE_F1,0x3b},{SDL_SCANCODE_F2,0x3c},{SDL_SCANCODE_F3,0x3d},{SDL_SCANCODE_F4,0x3e},
        {SDL_SCANCODE_F5,0x3f},{SDL_SCANCODE_F6,0x40},{SDL_SCANCODE_F7,0x41},{SDL_SCANCODE_F8,0x42},
        {SDL_SCANCODE_F9,0x43},{SDL_SCANCODE_F10,0x44},
        {SDL_SCANCODE_NUMLOCKCLEAR,0x45},{SDL_SCANCODE_SCROLLLOCK,0x46},
        {SDL_SCANCODE_KP_7,0x47},{SDL_SCANCODE_KP_8,0x48},{SDL_SCANCODE_KP_9,0x49},
        {SDL_SCANCODE_KP_MINUS,0x4a},
        {SDL_SCANCODE_KP_4,0x4b},{SDL_SCANCODE_KP_5,0x4c},{SDL_SCANCODE_KP_6,0x4d},
        {SDL_SCANCODE_KP_PLUS,0x4e},
        {SDL_SCANCODE_KP_1,0x4f},{SDL_SCANCODE_KP_2,0x50},{SDL_SCANCODE_KP_3,0x51},
        {SDL_SCANCODE_KP_0,0x52},{SDL_SCANCODE_KP_PERIOD,0x53},
        {SDL_SCANCODE_F11,0x57},{SDL_SCANCODE_F12,0x58},
        /* the grey cursor block: same base codes the BIOS reports after E0 */
        {SDL_SCANCODE_HOME,0x47},{SDL_SCANCODE_UP,0x48},{SDL_SCANCODE_PAGEUP,0x49},
        {SDL_SCANCODE_LEFT,0x4b},{SDL_SCANCODE_RIGHT,0x4d},
        {SDL_SCANCODE_END,0x4f},{SDL_SCANCODE_DOWN,0x50},{SDL_SCANCODE_PAGEDOWN,0x51},
        {SDL_SCANCODE_INSERT,0x52},{SDL_SCANCODE_DELETE,0x53},
        {SDL_SCANCODE_KP_ENTER,0x1c},{SDL_SCANCODE_KP_DIVIDE,0x35},
        {0,0}
    };
    int i;
    for (i = 0; m[i].sdl; i++) xt_of_sdl[m[i].sdl] = m[i].xt;
}

/* ASCII a DOS getch() would report, or 0 for an "extended" key. */
static unsigned char ascii_of(SDL_Keysym k)
{
    int shift = (k.mod & KMOD_SHIFT) != 0;
    SDL_Scancode s = k.scancode;
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z)
        return (unsigned char)((shift ^ ((k.mod & KMOD_CAPS) != 0) ? 'A' : 'a') + (s - SDL_SCANCODE_A));
    switch (s) {
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return '\r';
    case SDL_SCANCODE_ESCAPE:    return 27;
    case SDL_SCANCODE_BACKSPACE: return 8;
    case SDL_SCANCODE_TAB:       return 9;
    case SDL_SCANCODE_SPACE:     return ' ';
    default: break;
    }
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9)
        return shift ? (unsigned char)"!@#$%^&*("[s - SDL_SCANCODE_1] : (unsigned char)('1' + (s - SDL_SCANCODE_1));
    if (s == SDL_SCANCODE_0) return shift ? ')' : '0';
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9)
        return (SDL_GetModState() & KMOD_NUM) ? (unsigned char)('1' + (s - SDL_SCANCODE_KP_1)) : 0;
    if (s == SDL_SCANCODE_KP_0) return (SDL_GetModState() & KMOD_NUM) ? '0' : 0;
    switch (s) {
    case SDL_SCANCODE_MINUS:        return shift ? '_' : '-';
    case SDL_SCANCODE_EQUALS:       return shift ? '+' : '=';
    case SDL_SCANCODE_LEFTBRACKET:  return shift ? '{' : '[';
    case SDL_SCANCODE_RIGHTBRACKET: return shift ? '}' : ']';
    case SDL_SCANCODE_BACKSLASH:    return shift ? '|' : '\\';
    case SDL_SCANCODE_SEMICOLON:    return shift ? ':' : ';';
    case SDL_SCANCODE_APOSTROPHE:   return shift ? '"' : '\'';
    case SDL_SCANCODE_GRAVE:        return shift ? '~' : '`';
    case SDL_SCANCODE_COMMA:        return shift ? '<' : ',';
    case SDL_SCANCODE_PERIOD:       return shift ? '>' : '.';
    case SDL_SCANCODE_SLASH:        return shift ? '?' : '/';
    case SDL_SCANCODE_KP_DIVIDE:    return '/';
    case SDL_SCANCODE_KP_MULTIPLY:  return '*';
    case SDL_SCANCODE_KP_MINUS:     return '-';
    case SDL_SCANCODE_KP_PLUS:      return '+';
    default: return 0;
    }
}

/* A key event from whichever keyboard there is: into the INT 9 handler while
 * a game is running, into the BIOS buffer otherwise. */
static void key_event(const SDL_KeyboardEvent *k, int down)
{
    unsigned char xt = xt_of_sdl[k->keysym.scancode];
    if (raw_kbd) {
        if (xt) av_kbd_isr((unsigned char)(down ? xt : (xt | 0x80)));
    } else if (down) {                  /* typematic repeat included, like the BIOS buffer */
        unsigned char a = ascii_of(k->keysym);
        bk_val = (xt << 8) | a; bk_have = 1;
        if (a) kq_push(a);
        else if (xt) { kq_push(0); kq_push(xt); }
    }
}

/* ================================================================== */
/* PC speaker (both builds)                                            */
/*                                                                     */
/* sound(f) programmed PIT channel 2 with 1193181/f; the square wave   */
/* that divisor produces is synthesised here, in fixed point so the    */
/* same code runs in the board's mixer on core 1.  A one-pole low-pass */
/* takes the edge off without moving the pitch.                        */
/* ================================================================== */
static volatile unsigned spk_div;       /* 0 = silent */
static volatile int      sound_enabled = 1;
static unsigned          spk_phase;     /* 32-bit phase accumulator */
static int               spk_lp;
static int               audio_rate = 44100;
static int               audio_channels = 1;

#define SPK_AMPLITUDE 3800

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    Sint16 *out = (Sint16 *)stream;
    int frames = len / (int)sizeof(Sint16) / audio_channels, i, c;
    unsigned div = spk_div;
    unsigned step = 0;
    (void)ud;
    if (div && sound_enabled)
        step = (unsigned)((((unsigned long long)PIT_HZ) << 32) /
                          ((unsigned long long)div * (unsigned)audio_rate));
    for (i = 0; i < frames; i++) {
        int target = 0;
        if (step) {
            spk_phase += step;
            target = (spk_phase & 0x80000000u) ? -SPK_AMPLITUDE : SPK_AMPLITUDE;
        }
        spk_lp += ((target - spk_lp) * 45) / 128;
        for (c = 0; c < audio_channels; c++) *out++ = (Sint16)spk_lp;
    }
}

void plat_sound(int freq)
{
    if (freq <= 18) return;                 /* same guard as Turbo C's sound() */
    spk_div = PIT_HZ / (unsigned)freq;
}
void plat_nosound(void) { spk_div = 0; }
void plat_set_sound_enabled(int on) { sound_enabled = on; if (!on) spk_div = 0; }

/* ================================================================== */
/* The two back ends                                                   */
/* ================================================================== */
static void pump(void);
static void present(void);

#ifndef PICOSDL_SDL_H
/* ------------------------------------------------------------------ */
/* desktop SDL2                                                        */
/* ------------------------------------------------------------------ */
static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static Uint32        pal32[4];
static int           logical_h = CGA_H;
static SDL_AudioDeviceID audio_dev;

static int mouse_ok, mouse_dx_acc, mouse_btn;
static SDL_Joystick *joys[2];
static int joy_count;

static void die_now(void)
{
    plat_shutdown();
    exit(0);
}

static void pump(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            die_now();
            break;
        case SDL_MOUSEMOTION:
            mouse_dx_acc += e.motion.xrel;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) mouse_btn |= 1;
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) mouse_btn &= ~1;
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if ((e.key.keysym.mod & KMOD_ALT) && e.key.keysym.scancode == SDL_SCANCODE_F4 &&
                e.type == SDL_KEYDOWN) die_now();
            key_event(&e.key, e.type == SDL_KEYDOWN);
            break;
        default: break;
        }
    }
}

/* AV_DUMP=<file> writes every presented frame as raw RGB24 (or, if the name
 * ends in .hash, one 8-byte FNV-1a per frame); used by the A/B test harness to
 * compare against frames captured from DOSBox. */
static FILE *dumpf;
static int   dump_checked;
static int   dump_hash;

static void dump_frame(void)
{
    unsigned char row[CGA_W * 3];
    unsigned long long h = 1469598103934665603ULL;
    int x, y, i;

    for (y = 0; y < CGA_H; y++) {
        const unsigned char *src = cga_vram + ((y & 1) ? CGA_BANK : 0)
                                            + (y >> 1) * CGA_ROWBYTES;
        for (x = 0; x < CGA_W; x++) {
            const unsigned char *c = cga_palette[(src[x >> 2] >> (6 - 2 * (x & 3))) & 3];
            row[x * 3 + 0] = c[0];
            row[x * 3 + 1] = c[1];
            row[x * 3 + 2] = c[2];
        }
        if (dump_hash)
            for (i = 0; i < CGA_W * 3; i++) { h ^= row[i]; h *= 1099511628211ULL; }
        else
            fwrite(row, 1, sizeof row, dumpf);
    }
    if (dump_hash) {
        unsigned char out[8];
        for (i = 0; i < 8; i++) out[i] = (unsigned char)(h >> (8 * i));
        fwrite(out, 1, 8, dumpf);
    }
}

static void present(void)
{
    void *pixels;
    int pitch, x, y;

    if (SDL_LockTexture(tex, NULL, &pixels, &pitch) == 0) {
        for (y = 0; y < CGA_H; y++) {
            const unsigned char *src = cga_vram + ((y & 1) ? CGA_BANK : 0)
                                                + (y >> 1) * CGA_ROWBYTES;
            Uint32 *o = (Uint32 *)((Uint8 *)pixels + (size_t)y * pitch);
            for (x = 0; x < CGA_W; x += 4) {
                unsigned char b = src[x >> 2];
                o[x + 0] = pal32[(b >> 6) & 3];
                o[x + 1] = pal32[(b >> 4) & 3];
                o[x + 2] = pal32[(b >> 2) & 3];
                o[x + 3] = pal32[b & 3];
            }
        }
        SDL_UnlockTexture(tex);
    }

    if (!dump_checked) {
        const char *p = getenv("AV_DUMP");
        dump_checked = 1;
        if (p) {
            size_t n = strlen(p);
            dump_hash = (n > 5 && !strcmp(p + n - 5, ".hash"));
            dumpf = fopen(p, "wb");
        }
    }
    if (dumpf) dump_frame();

    SDL_RenderClear(ren);
    {
        SDL_Rect dst; dst.x = 0; dst.y = 0; dst.w = CGA_W; dst.h = logical_h;
        SDL_RenderCopy(ren, tex, NULL, &dst);
    }
    SDL_RenderPresent(ren);
}

int plat_can_quit(void) { return 1; }

void plat_default_controls(char *pl1, char *pl2) { *pl1 = 'K'; *pl2 = 'K'; }

int plat_mouse_present(void)  { return mouse_ok; }
void plat_set_mouse_grab(int on)
{
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    mouse_dx_acc = 0;
}
int plat_mouse_buttons(void)  { pump(); return mouse_btn; }
int plat_mouse_take_dx(void)  { int d; pump(); d = mouse_dx_acc; mouse_dx_acc = 0; return d; }

int plat_keyboard_present(void) { return 1; }

/* On a PC the menu slot is the mouse's; pads are joysticks, as they were. */
int plat_gamepad_present(void) { return 0; }
int plat_gamepad_button(void)  { return 0; }
int plat_gamepad_xaxis(void)   { return 0; }

int plat_joystick_present(void) { return joy_count > 0; }
int plat_joystick_button(int pad)
{
    SDL_Joystick *j = (pad < joy_count) ? joys[pad] : (joy_count ? joys[0] : NULL);
    int i, nb;
    if (!j) return 0;
    SDL_JoystickUpdate();
    nb = SDL_JoystickNumButtons(j);
    for (i = 0; i < nb && i < 4; i++) if (SDL_JoystickGetButton(j, i)) return 1;
    return 0;
}
int plat_joystick_xaxis(int pad)
{
    SDL_Joystick *j = (pad < joy_count) ? joys[pad] : (joy_count ? joys[0] : NULL);
    int v;
    if (!j) return 0;
    SDL_JoystickUpdate();
    v = SDL_JoystickGetAxis(j, 0);
    if (SDL_JoystickNumHats(j) > 0) {
        Uint8 h = SDL_JoystickGetHat(j, 0);
        if (h & SDL_HAT_LEFT)  return -1;
        if (h & SDL_HAT_RIGHT) return 1;
    }
    if (v < -8000) return -1;
    if (v >  8000) return 1;
    return 0;
}

int plat_init(int scale, int aspect43, int fullscreen)
{
    SDL_AudioSpec want, have;
    int w, h, i;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 0;
    }
    build_scancode_table();

    w = CGA_W * scale;
    h = aspect43 ? (w * 3 + 2) / 4 : CGA_H * scale;
    win = SDL_CreateWindow("Arcade Volleyball",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                           SDL_WINDOW_RESIZABLE |
                           (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 0; }

    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 0; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   /* nearest neighbour */
    logical_h = aspect43 ? 240 : CGA_H;     /* 240 => the original 4:3 CGA shape */
    SDL_RenderSetLogicalSize(ren, CGA_W, logical_h);

    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                            SDL_TEXTUREACCESS_STREAMING, CGA_W, CGA_H);
    if (!tex) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return 0; }

    for (i = 0; i < 4; i++)
        pal32[i] = 0xff000000u | ((Uint32)cga_palette[i][0] << 16) |
                   ((Uint32)cga_palette[i][1] << 8) | cga_palette[i][2];

    SDL_memset(&want, 0, sizeof want);
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1;
    want.samples = 512; want.callback = audio_cb;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev) {
        audio_rate = have.freq; audio_channels = have.channels;
        SDL_PauseAudioDevice(audio_dev, 0);
    } else {
        fprintf(stderr, "warning: no audio device (%s); running silently\n", SDL_GetError());
    }

    {   /* plenty of HID devices enumerate as joysticks; keep only real ones */
        int n = SDL_NumJoysticks();
        joy_count = 0;
        for (i = 0; i < n && joy_count < 2; i++) {
            SDL_Joystick *j = SDL_JoystickOpen(i);
            if (!j) continue;
            if (SDL_JoystickNumAxes(j) >= 2 && SDL_JoystickNumButtons(j) >= 1)
                joys[joy_count++] = j;
            else
                SDL_JoystickClose(j);
        }
    }

    /* a mouse is "installed" whenever we have a window to capture it in */
    mouse_ok = 1;
    SDL_SetRelativeMouseMode(SDL_FALSE);
    return 1;
}

void plat_shutdown(void)
{
    int i;
    if (audio_dev) { SDL_CloseAudioDevice(audio_dev); audio_dev = 0; }
    for (i = 0; i < joy_count; i++) if (joys[i]) SDL_JoystickClose(joys[i]);
    joy_count = 0;
    if (tex) { SDL_DestroyTexture(tex); tex = NULL; }
    if (ren) { SDL_DestroyRenderer(ren); ren = NULL; }
    if (win) { SDL_DestroyWindow(win); win = NULL; }
    SDL_Quit();
}

#else  /* PICOSDL_SDL_H */
/* ------------------------------------------------------------------ */
/* picosdl                                                             */
/*                                                                     */
/* The screen is one 320x200 canvas, a palette index a byte, handed to */
/* the panel with PSDL_PresentBuffer(); CGA's four colours are entries */
/* 0-3 of the display's CLUT.  A present only starts the transfer, so  */
/* a frame whose predecessor is still being read is skipped rather     */
/* than waited for: the game keeps the original's 60 Hz whatever the   */
/* panel manages, and since every present converts the whole of        */
/* cga_vram, a skipped frame loses nothing but itself.                 */
/*                                                                     */
/* Three input sources, each its own entry in the menu:                */
/*   Keyboard  a Bluetooth keyboard, selectable while one is connected; */
/*             it works exactly as the desktop's                        */
/*   Joystick  the board's analog stick, standing in for the PC's game  */
/*             port: left, right, and the click to jump                 */
/*   Gamepad   the I2C pad, in the slot a PC gives the mouse: its       */
/*             stick, A or B to jump                                    */
/* Outside a game all of them drive the menus as the keys the original  */
/* read - up and down as the cursor keys' extended codes, a button as   */
/* Enter.  In a game the pad's Start or Back leaves it, as Esc did.     */
/* ------------------------------------------------------------------ */
static Uint8  canvas[CGA_W * CGA_H];
static Uint32 expand[256];              /* one CGA byte -> four indices, little-endian */

static SDL_GameController *pad;        /* the I2C pad, if one answered at boot */
static SDL_Joystick       *stick;      /* the board's own analog stick          */

#define DEADZONE      12000
#define REPEAT_FIRST  400u              /* ms before a held direction repeats */
#define REPEAT_EVERY  120u

/* One frame's worth of every source, read once in pump(). */
typedef struct {
    int stick_x, stick_y, stick_btn;    /* the board's stick                   */
    int pad_x, pad_y, pad_btn;          /* the pad: its stick, A or B          */
    int leave;                          /* the pad's Start or Back             */
} Controls;
static Controls held;
static int      dir_held;               /* -1 up, +1 down, 0 none */
static Uint32   dir_next;               /* when the held direction repeats */

static int axis_dir(int v) { return v < -DEADZONE ? -1 : v > DEADZONE ? 1 : 0; }

/*
 * The pad's own stick, taken back out of the controller.  picosdl feeds both
 * the board's stick and the pad's into the controller's left stick, the larger
 * deflection winning per axis, so that a game reading only the controller
 * still hears the board.  Here each is a different player's, so the board's -
 * which picosdl also keeps on joystick 0 - is subtracted back out: a
 * controller axis that differs from the board's is the pad's exactly; one that
 * equals it means the pad was pushed no further than the board, which reads
 * as centred unless the board is centred too.
 */
static int pad_axis(int controller, int board)
{
    if (controller != board) return controller;
    return axis_dir(board) == 0 ? controller : 0;
}

static void read_controls(Controls *c)
{
    int bx = 0, by = 0;
    memset(c, 0, sizeof *c);
    if (stick) {
        bx = SDL_JoystickGetAxis(stick, 0);
        by = SDL_JoystickGetAxis(stick, 1);
        c->stick_x   = axis_dir(bx);
        c->stick_y   = axis_dir(by);
        c->stick_btn = SDL_JoystickGetButton(stick, 0) != 0;
    }
    if (pad) {
        c->pad_x = axis_dir(pad_axis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX), bx));
        c->pad_y = axis_dir(pad_axis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY), by));
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  c->pad_x = -1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) c->pad_x = 1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP))    c->pad_y = -1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  c->pad_y = 1;
        c->pad_btn = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) ||
                     SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B);
        c->leave   = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START) ||
                     SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK);
    }
}

/* Outside a game every source drives the menus, as the keys they expect:
 * up and down as the cursor keys' extended codes, any button as Enter. */
static void controls_to_keys(const Controls *now)
{
    Uint32 t = SDL_GetTicks();
    int y = now->stick_y ? now->stick_y : now->pad_y;
    int pressed = (now->stick_btn && !held.stick_btn) ||
                  (now->pad_btn   && !held.pad_btn)   ||
                  (now->leave     && !held.leave);

    if (raw_kbd) {
        /* in a game only leaving is a key: Esc, down and up, as INT 9 sees it */
        if (now->leave && !held.leave) { av_kbd_isr(0x01); av_kbd_isr(0x81); }
        dir_held = 0;
        return;
    }
    if (pressed) {
        kq_push('\r');
        bk_val = -1; bk_have = 1;           /* "Define Keys": keep the old key */
    }
    if (y != dir_held) {
        dir_held = y;
        dir_next = t + REPEAT_FIRST;
        if (dir_held) { kq_push(0); kq_push(dir_held < 0 ? 0x48 : 0x50); }
    } else if (dir_held && (Sint32)(t - dir_next) >= 0) {
        dir_next = t + REPEAT_EVERY;
        kq_push(0); kq_push(dir_held < 0 ? 0x48 : 0x50);
    }
}

static void pump(void)
{
    SDL_Event e;
    Controls now;
    SDL_PumpEvents();
    while (SDL_PollEvent(&e))
        if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)   /* a Bluetooth keyboard */
            key_event(&e.key, e.type == SDL_KEYDOWN);
    read_controls(&now);
    controls_to_keys(&now);
    held = now;
}

static void present(void)
{
    int y, x;
    if (PSDL_BufferBusy(canvas)) return;        /* the panel is still reading it */
    for (y = 0; y < CGA_H; y++) {
        const unsigned char *src = cga_vram + ((y & 1) ? CGA_BANK : 0)
                                            + (y >> 1) * CGA_ROWBYTES;
        Uint32 *o = (Uint32 *)(canvas + y * CGA_W);
        for (x = 0; x < CGA_ROWBYTES; x++) o[x] = expand[src[x]];
    }
    PSDL_PresentBuffer(canvas, CGA_W, CGA_H, CGA_W);
}

int plat_can_quit(void) { return 0; }       /* there is nowhere to quit to */

/* Nobody at a board can press Z, C or X without a Bluetooth keyboard: player
 * one gets the stick (or the pad, if that is all there is) and player two the
 * computer. */
void plat_default_controls(char *pl1, char *pl2)
{
    *pl1 = stick ? 'J' : pad ? 'G' : plat_keyboard_present() ? 'K' : 'C';
    *pl2 = (stick || pad || !plat_keyboard_present()) ? 'C' : 'K';
}

/* "Keyboard" while a Bluetooth keyboard is connected, asked of picosdl's
 * backend every time: it pairs some seconds after boot and can go away
 * again.  In a build without Bluetooth the answer is always no. */
bool psdl_pico_keyboard_connected(void);    /* picosdl/backend/pico/psdl_pico.h */

int plat_keyboard_present(void) { return psdl_pico_keyboard_connected() ? 1 : 0; }

int  plat_mouse_present(void)     { return 0; }
void plat_set_mouse_grab(int on)  { (void)on; }
int  plat_mouse_buttons(void)     { return 0; }
int  plat_mouse_take_dx(void)     { return 0; }

/* "Joystick" is the board's stick, which is what the PC's game port was. */
int plat_joystick_present(void)   { return stick != NULL; }
int plat_joystick_button(int p)   { (void)p; pump(); return held.stick_btn; }
int plat_joystick_xaxis(int p)    { (void)p; return held.stick_x; }

/* "Gamepad" is the I2C pad, in the menu slot a PC gives the mouse. */
int plat_gamepad_present(void)    { return pad != NULL; }
int plat_gamepad_button(void)     { pump(); return held.pad_btn; }
int plat_gamepad_xaxis(void)      { return held.pad_x; }

int plat_init(int scale, int aspect43, int fullscreen)
{
    SDL_AudioSpec want;
    SDL_Color c[4];
    int i;
    (void)scale; (void)aspect43; (void)fullscreen;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS |
                 SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 0;
    }
    build_scancode_table();

    for (i = 0; i < 4; i++) {
        c[i].r = cga_palette[i][0]; c[i].g = cga_palette[i][1];
        c[i].b = cga_palette[i][2]; c[i].a = SDL_ALPHA_OPAQUE;
    }
    SDL_SetPaletteColors(PSDL_GlobalPalette(), c, 0, 4);
    for (i = 0; i < 256; i++)
        expand[i] = (Uint32)((i >> 6) & 3)         | (Uint32)((i >> 4) & 3) << 8 |
                    (Uint32)((i >> 2) & 3) << 16   | (Uint32)(i & 3) << 24;
    memset(canvas, 0, sizeof canvas);

    /* Both, when both are there: they are different players' controls. */
    if (SDL_NumJoysticks() > 0) stick = SDL_JoystickOpen(0);
    if (SDL_IsGameController(0)) pad = SDL_GameControllerOpen(0);

    memset(&want, 0, sizeof want);
    want.freq = 22050; want.format = AUDIO_S16SYS; want.channels = 2;
    want.samples = 256; want.callback = audio_cb;
    if (SDL_OpenAudio(&want, NULL) == 0) {
        audio_rate = want.freq; audio_channels = 2;
        SDL_PauseAudio(0);
    } else {
        printf("audio: %s; running silently\n", SDL_GetError());
    }
    return 1;
}

void plat_shutdown(void)
{
    PSDL_PresentSync();
    SDL_CloseAudio();
    SDL_Quit();
}
#endif /* PICOSDL_SDL_H */

/* ================================================================== */
/* Pacing and the DOS console calls (both builds)                      */
/* ================================================================== */

/* The CGA retrace: present, then hold the game to 60 Hz. */
void plat_wait_retrace(void)
{
    static Uint64 next, period;
    Uint64 now;
    if (!period) {
        period = SDL_GetPerformanceFrequency() / FRAME_HZ;
        next = SDL_GetPerformanceCounter();
    }
    pump();
    present();
    if (plat_frame_hook) plat_frame_hook();
    next += period;
    now = SDL_GetPerformanceCounter();
    if (next < now) { next = now; return; }
    while ((now = SDL_GetPerformanceCounter()) < next) {
        Uint64 ms = (next - now) * 1000u / SDL_GetPerformanceFrequency();
        SDL_Delay(ms > 1 ? (Uint32)(ms - 1) : 0);
    }
}

int plat_kbhit(void) { pump(); return !kq_empty(); }

int plat_getch(void)
{
    for (;;) {
        int c;
        pump();
        c = kq_pop();
        if (c >= 0) return c;
        present();
        SDL_Delay(5);
    }
}

int plat_bioskey0(void)
{
    bk_have = 0;
    kq_head = kq_tail = 0;
    for (;;) {
        pump();
        if (bk_have) { kq_head = kq_tail = 0; return bk_val; }
        present();
        SDL_Delay(5);
    }
}

void plat_set_raw_kbd(int on) { pump(); raw_kbd = on; kq_head = kq_tail = 0; }

void plat_delay(int ms)
{
    Uint32 end = SDL_GetTicks() + (Uint32)ms;
    while ((Sint32)(end - SDL_GetTicks()) > 0) { pump(); SDL_Delay(1); }
}
