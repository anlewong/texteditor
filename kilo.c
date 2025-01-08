/*** Packages ***/

//Terminal Package
#include <termios.h>

//Basic Function Packages
#include <unistd.h>
#include <stdlib.h>

//Writing/IO Packages
#include <ctype.h>
#include <stdio.h>

//Error Handling Packages
#include <errno.h>

//Input Output control Package
#include <sys/ioctl.h>


/*** Definitions ***/
//Strips bits 5, 6

#define CTRL_KEY(k) ((k) & 0x1f)



/*** Data  ***/
//structs
struct editorConfig {
	int screenrows;
	int screencols;
	struct termios orig_termios;
};


//Global Data
struct editorConfig E;

/*** Helper Funcs ***/
void clearScreen();

/*** Terminal Functions ***/

//Handling Errors
void die(const char*s){

	//clear screen
	clearScreen();	
	
	//Prints s as an error message
	perror(s);
	exit(1);
}


//setting up terminal for manipulatoin
void disableRawMode(){
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

void enableRawMode(){
       	
	//get the initial terminal state and flags.
	if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	
	//upon exit restor to initial terminal condition
	atexit(disableRawMode); 
	
	struct termios raw = E.orig_termios; //set manipulated terminal to original
	//c_iflag deals with terminal input characteristics	
	//IXON flag for data flow control, related to ctrl-s, ctrl-q
	//ICRNL turns carraige -> newline translation
	//BRKINT controls if break cond functions like ctrl-c
	//INPCK parity checking for outdated terminals
	//ISTRIP removes 8th bit of each input byte
	raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
	
	//c_oflag deals with output behavior
	//OPOST flag for "\n" -> "\r\n"
	raw.c_oflag &= ~(OPOST);

	//c_cflag character flag
	//CS8 is bitmask so all chars are 8 bits per byte	
	raw.c_cflag |= (CS8);

	//c_lflag deals with local behavior 
	//ECHO controls if terminal shows what is typed
	//ICANON flag for canonical mode
	//IEXTEN controls extended inputs from ctrl-v and ctrl-0 for mac
	//ISIG turns off processing of signal commands, ctrl-c terminate and ctrl-z suspend
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

	//timeout behavior
	//VMIN sets minum bytes for read before return to 0
	//VTIME sets read wait to 1/10 of a second
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;	
	
	//set curr terminal to new flags
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

char editorReadKey(){
	int nread;
	char c;
	//Spin lock till c is valid character
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
		if (nread == -1 && errno != EAGAIN) die("read");
	}
	return c;
}

int getCursorPosition(int *rows, int *cols) {
	char buf[32];
	unsigned int i = 0;
	
	//returns cursor posiiton to std out
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
	
	//reads cursor location into buf;
	while(read(STDIN_FILENO, &buf[i], 1) == 1){
		if(buf[i] == 'R') break;
		i++;
	}	
	
	//sets last char so string interpetable	
	buf[i] = '\0';

	//ensure return argument is a command sequence	
	if(buf[0] != '\x1b' || buf[1] != '[') return -1;

	//Ensure return argument finds two digits, one for row and cols
 	if(sscanf(&buf[2], "%d;%d", rows, cols) == -2) return -1;

	return 0;
}

/*** output ***/
void clearScreen(){
	write(STDOUT_FILENO, "\x1B[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
}

//grab window size from os
int getWindowSize(int *rows, int *cols) {
	//Window size struct holds cols and rows
	struct winsize ws;

	//ioctl - input output control,(fd, op, &stuct);
	//TIOCGWINSZ - request window size of file
	
	//1 || means always true, this code, moves the cursor to the bottom right part of the screen, and errors on a failure. It then reads the key inputed.
	if (1 || ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		//C cursor foward, 1 arg
		//B cursor down, 1 arg
		if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

void editorDrawRows() {
	//print tildas for y rows
	int y;
	for (y=0; y<E.screenrows; y++){
		write(STDOUT_FILENO, "~\r\n",3);
	}
}

void editorRefreshScreen() {
	// \x1b - escape character
	// \x1b[ - escape sequence, terminal formatting command
	// J - Erase In Display, 2 - erase entire screen
	write(STDOUT_FILENO, "\x1B[2J", 4);
	
	//H - cursor position
	//H Args, height;widthH
	write(STDOUT_FILENO, "\x1b[H", 3);

	editorDrawRows();

	//Reposition Cursor Top Left	
	write(STDOUT_FILENO, "\x1b[H", 3);
}


/*** input ***/
void editorProcessKeypress(){
	//get c from editor
	char c = editorReadKey();
	
	//if c is a hotkey, apply case behavior
	switch (c) {
		case CTRL_KEY('q'):
			clearScreen();
			exit(0);
			break;
	}
}


/*** Initialization ***/
void initEditor(){
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(){
	//enable editor mode
	enableRawMode();
	initEditor();
	
	//go till break
	while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}
