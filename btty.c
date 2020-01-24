#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <menu.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define die(...) endwin(), fprintf(stderr, __VA_ARGS__), exit(1)

typedef struct point { int x, y; } point;

void init_cga()
{
    // init CGA color map
    char cga[] = { COLOR_BLACK, COLOR_BLUE, COLOR_GREEN, COLOR_CYAN, COLOR_RED, COLOR_MAGENTA, COLOR_YELLOW, COLOR_WHITE };
    start_color();
    for (int fg = 0; fg < 8; fg++)
        for (int bg = 0; bg < 8; bg++)
            init_pair((bg<<4)+fg, cga[fg], cga[bg]);
}

void set_cga(int attr)
{
    // set CGA attribute
    attron(COLOR_PAIR(attr & 0x77));
    if (attr & 8) attron(A_BOLD); else attroff(A_BOLD);
    if (attr & 0x80) attron(A_BLINK); else attroff(A_BLINK);
}

void draw_border(point *console, point *terminal)
{
    if (terminal->x > console->x || terminal->y > console->y) die("Terminal size is %dx%d, but your console is only %dx%d\n", terminal->y, terminal->x, console->y, console->x);
    set_cga(15);
    if (terminal->y < console->y) for (int x=0; x < terminal->x && x < console->x; x++) mvaddch(terminal->y, x, ACS_HLINE);
    if (terminal->x < console->x) for (int y=0; y < terminal->y && y < console->y; y++) mvaddch(y, terminal->x, ACS_VLINE);
    if (terminal->x < console->x && terminal->y < console->y) mvaddch(terminal->y, terminal->x, ACS_LRCORNER);
}

// Query the vcsa and return terminal size, cursor location, and content. Note
// request pointers may be NULL. *content must be free'd by caller.
void get_vcsa(int vcsa, point *terminal, point *cursor, uint16_t **content)
{
    struct { uint8_t lines, columns, atx, aty; } attr;
    lseek(vcsa, 0, 0);
    if (read(vcsa, &attr, 4) != 4) die("Error reading vcsa attributes: %s\n", strerror(errno));
    if (terminal) { terminal->x = attr.columns; terminal->y = attr.lines; }
    if (cursor) { cursor->x = attr.atx; cursor->y = attr.aty; }
    if (content)
    {
        int get = attr.lines * attr.columns * 2;
        *content=malloc(get);
        if (!*content) die("Out of memory!\n");
        if (read(vcsa, *content, get) != get) die("Error reading vcsa content\n", strerror(errno));
    }
}

int main(int argc, char *argv[])
{
    int X=1;
    if (argc > 1) X=atoi(argv[1]);

    // Open /dev/ttyX and /dev/vcsaX
    char *stty, *svcsa;
    if (asprintf(&svcsa, "/dev/vcsa%d", X) <= 0) die("asprintf failed\n");
    if (asprintf(&stty, "/dev/tty%d", X) <= 0) die("asprintf failed\n");

    int tty = open(stty, O_RDONLY);
    if (tty < 0) die ("can't open %s: %s\n", stty, strerror(errno));
    if (flock(tty, LOCK_EX|LOCK_NB)) die("%s: %s\n", stty, strerror(errno));

    int vcsa = open(svcsa, O_RDONLY);
    if (vcsa < 0) die ("can't open %s: %s\n", svcsa, strerror(errno));

    // initialize curses
    initscr();
    noecho();
    raw();
    timeout(20);
    set_escdelay(100);
    init_cga();

    // console lr corner
    point console;
    getmaxyx(stdscr, console.y, console.x);

    // terminal lr corner
    point terminal;
    get_vcsa(vcsa, &terminal, NULL, NULL);
    draw_border(&console, &terminal);

    while (1)
    {
        point cursor;
        uint16_t *content, *b;
        get_vcsa(vcsa, &terminal, &cursor, &content);
        b = content;
        for (int y=0; y < terminal.y; y++) for (int x=0; x < terminal.x; x++)
        {
            uint16_t d = *b++;
            set_cga(d >> 8);         // high byte is the color
            mvaddch(y, x, d & 255);  // low byte is the glyph
        }
        free(content);
        move(cursor.y, cursor.x);
        refresh();

        int k = getch();
        switch(k)
        {
            case ERR: break;
            case 29: goto done;
            default:
                if (k < 128 && ioctl(tty, TIOCSTI, &k) < 0) die("TIOCSTI failed (do you need root?): %s\n", strerror(errno));
                if (k == 12)
                {
                    clear();
                    draw_border(&console, &terminal);
                }
                break;
        }
    }

    done:
    endwin();
    printf("Done\n");
    return 0;
}


