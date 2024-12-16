//main custom text editor

//imported  libraries
#define _DEFAULT_SOURCE
#define   _BSD_SOURCE
#include <termios.h>
#include <unistd.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

//ctrl key definitions
//#define CTRL_KEY(k) ((k) & 0x1f)
#define CTRL_KEY(k) ((k) & 0x1f)

//data
struct  editorConfig    
{
    int screenrows;
    int screencols;
    struct termios original;
};

struct editorConfig E;

//errror handling
void die(const char *s){
    //erase screen on exit
    write(STDOUT_FILENO, "\x1b[2j", 4);
    write(STDOUT_FILENO, "x1b[H", 3);

    //error handling/ending
    perror(s);
    exit(1);
}

//terminal setting handling
void disable_raw_mode(void){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.original) < 1){
        die("tcsetattr");
    }
}

void set_raw_mode(void){
    if(tcgetattr(STDIN_FILENO, &E.original) < 0){
        die("tcgetattr");
    }
    atexit(disable_raw_mode);

    struct termios raw = E.original;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if(tcsetattr(STDOUT_FILENO, TCSAFLUSH, &raw) < 0){
        die("tcsetattr");
    }

}

char editor_read_key(void){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

int getCursorPosition(int *rows, int *cols){
    if(write(STDOUT_FILENO, "x1b[6n", 4) != 4) return -1;
    printf("\r\n");
    char c;
    while(read(STDIN_FILENO, &c, 1 == 1)){
        if(iscntrl(c)){
            printf("%d\r\n", c);
        } else{
            printf("%d('%c')\r\n", c, c);
        }
    }
    editor_read_key();
    return -1;
}


int getWindowSize(int *rows, int *cols){
    struct winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}


//output based on VT100 documentation
void editor_draw_rows(void){
    int y;
    for (y=0; y <24; y++) {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void editor_refresh_screen(void){
    write(STDOUT_FILENO, "\x1b[2j", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    editor_draw_rows();
    write(STDOUT_FILENO, "\x1b[H", 3);
}


//input handling
void editor_process_keypress(void){
    char c = editor_read_key();
    switch (c)
    {
    case (CTRL_KEY('q')):
        write(STDOUT_FILENO, "\x1b[2j", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
    }

}

//nit
void initEditor(void){
    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(){
    set_raw_mode();
    initEditor();

    while(1){
        editor_refresh_screen();
        editor_process_keypress();
    }
    return 0;
}