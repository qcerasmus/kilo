/* includes */
#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* defines */
#define TE_VERSION "0.0.1"
#define CTRL_KEY(k) ((k)&0x1f)
enum EditorKey
{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
		DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/* data */
struct editorConfig
{
    int cx, cy;
    int screenrows;
    int screencols;
    struct termios original_termios;
};

struct editorConfig E;

/* terminal */
void Die(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void DisableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.original_termios) == -1)
        Die("tcsetattr");
}

void EnableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.original_termios) == -1)
        Die("tcgetattr");
    atexit(DisableRawMode);

    struct termios raw = E.original_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        Die("tcsetattr");
}

int EditorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            Die("read");
    }
    if (c == '\x1b')
    {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';
        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                    case '1':
                        return HOME_KEY;
										case '3':
												return DEL_KEY;
                    case '4':
                        return END_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '7':
                        return HOME_KEY;
                    case '8':
                        return END_KEY;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O')
        {
            switch (seq[1])
            {
							case 'H': return HOME_KEY;
							case 'F': return END_KEY;
            }
        }

        return '\x1b';
    }
    else
    {
        return c;
    }
}

int GetCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;
    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;
    return 0;
}

int GetWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;

        return GetCursorPosition(rows, cols);
    }

    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

/* append buffer */
struct abuf
{
    char *b;
    int len;
};

#define ABUF_INIT \
    {             \
        NULL, 0   \
    }

void abAppend(struct abuf *ab, const char *s, int len)
{
    char *newMemory = realloc(ab->b, ab->len + len);
    if (newMemory == NULL)
        return;

    memcpy(&newMemory[ab->len], s, len);
    ab->b = newMemory;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

/* output */
void EditorDrawRows(struct abuf *ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        if (y == E.screenrows / 3)
        {
            char welcome[80];
            int welcomeLen = snprintf(welcome, sizeof(welcome), "TE -- version %s", TE_VERSION);
            if (welcomeLen > E.screencols)
                welcomeLen = E.screencols;
            int padding = (E.screencols - welcomeLen) / 2;
            if (padding)
            {
                abAppend(ab, "~", 1);
                padding--;
            }
            while (padding--)
                abAppend(ab, " ", 1);
            abAppend(ab, welcome, welcomeLen);
        }
        else
        {
            abAppend(ab, "~", 1);
        }
        abAppend(ab, "\x1b[K", 3);
        if (y < E.screenrows - 1)
            abAppend(ab, "\r\n", 2);
    }
}

void EditorRefreshScreen()
{
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);
    EditorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    // abAppend(&ab, "\x1b[H", 3);
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/* input */
void EditorMoveCursor(int key)
{
    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx != 0)
            E.cx--;
        break;
    case ARROW_RIGHT:
        if (E.cx != E.screencols - 1)
            E.cx++;
        break;
    case ARROW_UP:
        if (E.cy != 0)
            E.cy--;
        break;
    case ARROW_DOWN:
        if (E.cy != E.screenrows - 1)
            E.cy++;
        break;
    }
}

void EditorProcessKeypress()
{
    int c = EditorReadKey();
    switch (c)
    {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
		case HOME_KEY:
				E.cx = 0;
				break;
		case END_KEY:
				E.cx = E.screencols - 1;
				break;
    case PAGE_UP:
    case PAGE_DOWN: {
        int times = E.screenrows;
        while (times--)
            EditorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        break;
    }
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        EditorMoveCursor(c);
        break;
    }
}

/* initialisation */
void InitEditor()
{
    E.cx = 0;
    E.cy = 0;
    if (GetWindowSize(&E.screenrows, &E.screencols) == -1)
        Die("GetWindowSize");
}

int main()
{
    EnableRawMode();
    InitEditor();

    while (1)
    {
        EditorRefreshScreen();
        EditorProcessKeypress();
    }

    return 0;
}
