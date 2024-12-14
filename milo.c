//main custom text editor

//imported  libraries
#define _DEFAULT_SOURCE
#define   _BSD_SOURCE
#include <unistd.h>
#include <termios.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>


struct termios original;

//errror handling
void die(const char *s){
    perror(s);
    exit(1);
}

//terminal setting handling
void disable_raw_mode(void){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &original) < 1){
        die("tcsetattr");
    }
}

void set_raw_mode(void){
    if(tcgetattr(STDIN_FILENO, &original) < 0){
        die("tcgetattr");
    }
    atexit(disable_raw_mode);

    struct termios raw = original;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if(tcsetattr(STDOUT_FILENO, TCSAFLUSH, &raw) < 0){
        die("tcsetattr");
    }

}

//main function that deals with implementation
int main(){
    
    set_raw_mode();

    while(1){
        char c = '\0';
        if(read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN){
            die("read");
        }
        if(iscntrl(c)){
            printf("%d\r\n", c);
        }
        else {
            printf("%d ('%c')\r\n", c, c);
        }
        if(c == 'q'){
            break;
        }
    }
}