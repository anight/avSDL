/* plat.c - SDL2 platform layer.  Software rendering only. */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "plat.h"
#include "cga.h"

#define PIT_HZ      1193181
#define FRAME_HZ    60.0
#define AUDIO_RATE  44100

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static Uint32        pal32[4];
static int           logical_h = CGA_H;

static int  raw_kbd;
static int  quitting;

/* ---- getch queue (cooked mode) ---- */
#define QSZ 64
static unsigned char kq[QSZ];
static int kq_head, kq_tail;
static void kq_push(unsigned char c) { int n = (kq_tail + 1) % QSZ; if (n != kq_head) { kq[kq_tail] = c; kq_tail = n; } }
static int  kq_empty(void) { return kq_head == kq_tail; }
static int  kq_pop(void) { int c; if (kq_empty()) return -1; c = kq[kq_head]; kq_head = (kq_head + 1) % QSZ; return c; }

/* bioskey(0) wants the raw (scancode, ascii) pair */
static int  bk_have, bk_val;

/* ---- mouse / joystick ---- */
static int mouse_ok, mouse_dx_acc, mouse_btn;
static SDL_Joystick *joys[2];
static int joy_count;

/* ---- PC speaker ---- */
static SDL_AudioDeviceID audio_dev;
static volatile int spk_div;        /* 0 = silent */
static double spk_phase;
static int sound_enabled = 1;
static double spk_lp;

/* ------------------------------------------------------------------ */
/* XT (set 1) scancode table indexed by SDL scancode.                  */
/* ------------------------------------------------------------------ */
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

static void die_now(void)
{
    quitting = 1;
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
        case SDL_KEYUP: {
            unsigned char xt = xt_of_sdl[e.key.keysym.scancode];
            if ((e.key.keysym.mod & KMOD_ALT) && e.key.keysym.scancode == SDL_SCANCODE_F4 &&
                e.type == SDL_KEYDOWN) die_now();
            if (raw_kbd) {
                if (xt) av_kbd_isr((unsigned char)(e.type == SDL_KEYDOWN ? xt : (xt | 0x80)));
            } else if (e.type == SDL_KEYDOWN) {   /* typematic repeat included,
                                                     like the BIOS buffer */
                unsigned char a = ascii_of(e.key.keysym);
                bk_val = (xt << 8) | a; bk_have = 1;
                if (a) kq_push(a);
                else if (xt) { kq_push(0); kq_push(xt); }
            }
            break;
        }
        default: break;
        }
    }
}

/* ------------------------------------------------------------------ */
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

void plat_wait_retrace(void)
{
    static Uint64 next;
    static double perf;
    Uint64 now;
    if (!perf) { perf = (double)SDL_GetPerformanceFrequency(); next = SDL_GetPerformanceCounter(); }
    pump();
    present();
    next += (Uint64)(perf / FRAME_HZ);
    now = SDL_GetPerformanceCounter();
    if (next < now) { next = now; return; }
    while ((now = SDL_GetPerformanceCounter()) < next) {
        double ms = (double)(next - now) * 1000.0 / perf;
        if (ms > 2.0) SDL_Delay((Uint32)(ms - 1.0));
        else SDL_Delay(0);
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

/* ------------------------------------------------------------------ */
/* PC speaker                                                          */
/* ------------------------------------------------------------------ */
static void audio_cb(void *ud, Uint8 *stream, int len)
{
    Sint16 *out = (Sint16 *)stream;
    int n = len / (int)sizeof(Sint16), i;
    int div = spk_div;
    double freq = (div && sound_enabled) ? (double)PIT_HZ / div : 0.0;
    double step = freq / AUDIO_RATE;
    (void)ud;
    for (i = 0; i < n; i++) {
        double target = 0.0;
        if (freq > 0.0) {
            spk_phase += step;
            if (spk_phase >= 1.0) spk_phase -= 1.0;
            target = (spk_phase < 0.5) ? 1.0 : -1.0;
        }
        /* one-pole smoothing keeps the square wave from sounding harsh
         * without changing its pitch */
        spk_lp += (target - spk_lp) * 0.35;
        out[i] = (Sint16)(spk_lp * 3800.0);
    }
}

void plat_sound(int freq)
{
    if (freq <= 18) return;                 /* same guard as Turbo C's sound() */
    spk_div = PIT_HZ / freq;
}
void plat_nosound(void) { spk_div = 0; }
void plat_set_sound_enabled(int on) { sound_enabled = on; if (!on) spk_div = 0; }

/* ------------------------------------------------------------------ */
int plat_mouse_present(void)  { return mouse_ok; }
void plat_set_mouse_grab(int on)
{
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    mouse_dx_acc = 0;
}
int plat_mouse_buttons(void)  { pump(); return mouse_btn; }
int plat_mouse_take_dx(void)  { int d; pump(); d = mouse_dx_acc; mouse_dx_acc = 0; return d; }

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

/* ------------------------------------------------------------------ */
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
    want.freq = AUDIO_RATE; want.format = AUDIO_S16SYS; want.channels = 1;
    want.samples = 512; want.callback = audio_cb;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev) SDL_PauseAudioDevice(audio_dev, 0);
    else fprintf(stderr, "warning: no audio device (%s); running silently\n", SDL_GetError());

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
