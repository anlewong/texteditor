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
#include <string.h>

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
    write(STDOUT_FILENO, "\x1b[2j", 4);
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
    char buf[32];
    unsigned int i = 0;

    if(write(STDOUT_FILENO, "x1b[6n", 4) != 4) return -1;
    
    while (i < sizeof(buf) -1){
        if(read(STDIN_FILENO, &buf, 1) != 1) break;
        if(buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;
}


int getWindowSize(int *rows, int *cols){
    struct winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        getCursorPosition(rows, cols);
    } else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
    return -1;
}

/*** append buffer code, allow for entire screen to be written at once **/
struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {Null, 0}

void abAppend(struct  abuf *ab, const char *s, int len){
	char *new = realloc(ab->b, ab->len + len);
	
	if (new == NULL) return;

	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abfree(struct abuf *ab){
	free(ab->b);
	return;
}

//output based on VT100 documentation
void editor_draw_rows(void){
    int y;
    for (y=0; y < E.screenrows; y++) {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void editor_refresh_screen(void){
    write(STDOUT_FILENO, "\x1b[2j", 4);
    //write(STDOUT_FILENO, "\x1b[H", 3);

    editor_draw_rows();
    write(STDOUT_FILENO, "\x1b[H", 3);
}


//input handling
void editor_process_keypress(void){
    char c = editor_read_key();
    

}

//nit
void initEditor(void){
    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(){
    set_raw_mode();
    //initEditor();
    
    while(1){
        //editor_refresh_screen();
        //editor_process_keypress();
	
	char c = '\0';
	read(STDIN_FILENO, &c, 1);
	if(iscntrl(c)){
		printf("%d\r\n", c);
	} else {
		printf("%d ('%c')\r\n", c, c);	
   	}
	if (c == 'q') break;
  }
    return 0;
}
