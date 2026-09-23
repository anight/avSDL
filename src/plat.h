/* plat.h - platform layer (SDL2) standing in for the DOS services the
 * original used: the CGA vertical-retrace wait, INT 9 keyboard, DOS
 * kbhit/getch, INT 16h bioskey, INT 33h mouse, the game-port joystick,
 * the 8253 PC speaker and Turbo C's delay(). */
#ifndef PLAT_H
#define PLAT_H

int  plat_init(int scale, int aspect43, int fullscreen);
void plat_shutdown(void);

/* Present the frame and pace to the 60 Hz CGA retrace. */
void plat_wait_retrace(void);

/* DOS console input */
int  plat_kbhit(void);
int  plat_getch(void);
int  plat_bioskey0(void);        /* (scancode << 8) | ascii, or -1: cancelled */

/* INT 9 emulation: while raw mode is on, key presses/releases are delivered
 * to av_kbd_isr() as XT make/break scancodes and never reach the getch
 * queue - exactly like installing your own int 9 handler under DOS. */
void plat_set_raw_kbd(int on);
void av_kbd_isr(unsigned char scancode);   /* implemented by the game */

void plat_delay(int ms);

/* Called once a frame, after the present, if set: a board reports on itself
 * from here without the game knowing anything about the board. */
extern void (*plat_frame_hook)(void);

/* 0 where "Exit" has nowhere to go (a board): the menu just comes back. */
int  plat_can_quit(void);

/* The controls each player starts with, as the fifth character of the menu
 * entry: 'K'eyboard, 'J'oystick, ' ' mouse or 'C'omputer. */
void plat_default_controls(char *pl1, char *pl2);

/* PC speaker */
void plat_sound(int freq);
void plat_nosound(void);
void plat_set_sound_enabled(int on);

/* INT 33h mouse */
int  plat_mouse_present(void);
void plat_set_mouse_grab(int on);
int  plat_mouse_buttons(void);
int  plat_mouse_take_dx(void);

/* game port */
int  plat_joystick_present(void);
int  plat_joystick_button(int pad);
int  plat_joystick_xaxis(int pad);       /* -1, 0, +1 */

#endif
