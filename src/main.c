/* main.c - entry point / command line for the SDL2 port of Arcade Volleyball. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "plat.h"

int av_main(void);

static void usage(const char *p)
{
    printf("Arcade Volleyball (SDL2 software-rendered port of the 1987 DOS game)\n\n"
           "usage: %s [options]\n"
           "  -s, --scale N    integer window scale, default 3 (320x200 -> 960x600)\n"
           "  -a, --aspect     stretch to the original 4:3 CGA aspect ratio\n"
           "  -f, --fullscreen start fullscreen\n"
           "  -h, --help       this text\n\n"
           "Controls\n"
           "  Player 1 : Z / C move, X jump\n"
           "  Player 2 : keypad 1 / 3 move, keypad 2 jump\n"
           "  Menu     : up/down arrows (or 8/2) to move, Enter to pick or cycle\n"
           "  Esc      : leave the current game and go back to the menu\n"
           "Use \"Define Keys\" in the menu to rebind; \"Exit\" quits.\n", p);
}

int main(int argc, char **argv)
{
    int scale = 3, aspect = 0, full = 0, i, rc;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(argv[0]); return 0; }
        else if (!strcmp(a, "-a") || !strcmp(a, "--aspect")) aspect = 1;
        else if (!strcmp(a, "-f") || !strcmp(a, "--fullscreen")) full = 1;
        else if ((!strcmp(a, "-s") || !strcmp(a, "--scale")) && i + 1 < argc) {
            scale = atoi(argv[++i]);
            if (scale < 1) scale = 1;
            if (scale > 10) scale = 10;
        } else { fprintf(stderr, "unknown option: %s\n", a); usage(argv[0]); return 2; }
    }

    if (!plat_init(scale, aspect, full)) return 1;
    rc = av_main();
    plat_shutdown();
    return rc;
}
