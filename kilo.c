/*** Packages ***/

//Terminal Package
#include <termios.h>

//Basic Function Packages
#include <unistd.h>
#include <stdlib.h>

//Writing/IO Packages
#include <ctype.h>
#include <stdio.h>

//writing with buff
#include <string.h>

//Error Handling Packages
#include <errno.h>

//Input Output control Package
#include <sys/ioctl.h>


/*** Definitions ***/
#define KILO_VERSION "0.0.1"

//Strips bits 5, 6
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey{
	ARROW_UP =  1000,
	ARROW_LEFT, 
	ARROW_DOWN, 
	ARROW_RIGHT,
	PAGE_UP,
	PAGE_DOWN,
	HOME_KEY,
	END_KEY,
	DELETE_KEY
};




/*** Data  ***/

typedef struct erow {
	int size;
	char *chars;
} erow;

struct editorConfig {
	int cx, cy;
	int screenrows;
	int screencols;
	
	int numrows;
	erow row;

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

int editorReadKey(){
	int nread;
	char c;
	//Spin lock till c is valid character
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
		if (nread == -1 && errno != EAGAIN) die("read");
	}
	
	//check if escape seq
	if(c == '\x1b'){
		//hold potential special commands
		char seq[3];
		
		//make sure it reads some arg after escape char
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		//valid seq
		if (seq[0] == '['){
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~'){
					//Page swithc, denoted esc[5~ or esc[6~. 3 char seq
					switch (seq[1]){
						case '1': return HOME_KEY;
						case '3': return DELETE_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}	
				}
			}  else {
				//arrow key swithc, denoted by esc[A 2 char seq
				switch (seq[1]){
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		} 
		//needed because sometimes doesn't have {
		//Home esc[1~, 7~ or [h, or escOH, 
		//END esc[4~, or 8~, or [F or OF
		else if (seq[0] == 'O'){
			switch (seq[1]){
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}

		return '\x1b';
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
	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
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

/***file i/o ***/

//function to modify editor rows
void editorOpen(){
	//line we want to see in our editor
	char *line = "Hello, world!";

	//length of our line including space for a terminating char
	ssize_t linelen = 13;

	//setting one of our row objects sizes = to linelen to know how much to print out
	E.row.size = linelen;

	//allocating memory for the string, will realloc when editing to match what is written
	E.row.chars = malloc(linelen + 1);

	//copying our string into the char* pointer in the row object,
	memcpy(E.row.chars, line, linelen);

	//ensure our string is null terminated
	E.row.chars[linelen] = '\0';
	
	//updating our row count
	E.numrows = 1;

}



/*** append buffer ***/
struct abuf{
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, char *s, int len){
	//allocate mem to hold old and new string
	char *new = realloc(ab->b, ab->len + len);
	
	//err handle if realloc fail
	if(new == NULL) return;

	//copy the new string to the end of the old one
	memcpy(&new[ab->len], s, len);

	//set abuf to bethe new string pointer an dlength
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab){
	free(ab->b);
}

void editorDrawRows(struct abuf *ab) {
	//print tildas for y rows
	int y;

	//loop through all visible rows to print
	for (y=0; y<E.screenrows; y++){

		//check if current row has an existing erow to print
		if(y >= E.numrows){

			//Print Centered Welcome Message at top third of editor in center
			if (y == E.screenrows/3){
				//welcome string, name and version
				char welcome[80];

				//welcomelen equals whatever snprintf was able to write into welcome
				int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo Editor -- version %s", KILO_VERSION);
			
				//truncates the welcome message to screencols
				if (welcomelen > E.screencols) welcomelen = E.screencols;
			
				//calc middle of screen
				int padding = (E.screencols - welcomelen)/2;
				if (padding){
					abAppend(ab, "~", 1);
					padding--;
				}
				while(padding--) abAppend(ab, " ", 1);	
				abAppend(ab, welcome, welcomelen);
			} else {
				abAppend(ab, "~", 1);		
			}	
		} else {

			//only append erow content that can be read given editor window size
			int len = E.row.size;
			if (len > E.screencols) len = E.screencols;
			abAppend(ab, E.row.chars, len);
		}
		//K erases everything to the right of the cursor, cursor moves with printing text
		abAppend(ab, "\x1b[K" ,3);

		//iterate to next column via return + carriage.
		if(y < E.screenrows -1){
			abAppend(ab, "\r\n", 3);
		}	
				
	}
}

void editorRefreshScreen() {
	//init buf
	struct abuf ab = ABUF_INIT;
	
	//?25l hides cursor/doesn't display
	abAppend(&ab, "\x1b[?25l", 6);


	// \x1b - escape character
	// \x1b[ - escape sequence, terminal formatting command
	// J - Erase In Display, 2 - erase entire screen
	//abAppend(&ab, "\x1B[2J", 4);
	
	//H - cursor position
	//H Args, height;widthH
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);

	//Reposition Cursor cx, cy
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy+1, E.cx+1);
	abAppend(&ab, buf, strlen(buf));
	
	//?25h unhides cursor
	abAppend(&ab, "\x1b[?25h", 6);
	
	//write buf out
	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}


/*** input ***/
void editorMoveCursor(int key){
	switch (key) {
		case ARROW_UP:
			if(E.cy > 0){
        			E.cy--;
			}
        		break;
        	case ARROW_LEFT:
			if(E.cx > 0){
				E.cx--;
			}
        		break;
        	case ARROW_DOWN:
			if(E.cy < E.screenrows){
				E.cy++;
			}
        		break;
        	case ARROW_RIGHT:
			if(E.cx < E.screencols){
				E.cx++;
			}
        		break;
	}
}

void editorProcessKeypress(){
	//get c from editor
	int c = editorReadKey();
	
	//if c is a hotkey, apply case behavior
	switch (c) {
		case CTRL_KEY('q'):
			clearScreen();
			exit(0);
			break;
		
		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			E.cx = E.screencols - 1;
			break;

		case PAGE_UP:
			E.cy = 0;
			break;
		case PAGE_DOWN:
			E.cy = E.screenrows - 1;
			break;
		
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);	
			break;
	}
}


/*** Initialization ***/
void initEditor(){
	E.cx = 0;
	E.cy = 0;
	E.numrows = 0;
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(){
	//enable editor mode
	enableRawMode();
	initEditor();
	editorOpen();	


	//go till break
	while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}

