#pragma region /*** Packages ***/
//Terminal Package
#include <termios.h>

//Basic Function Packages
#include <unistd.h>
#include <stdlib.h>

//feature test macros tried to get getline working always throwing a fit
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#define _XOPEN_SOURCE >= 500

//Writing/IO Packages
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

//writing with buff
#include <string.h>

//Error Handling Packages
#include <errno.h>

//Input Output control Package
#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>

//For Status Bar
#include <time.h>
#include <stdarg.h>

#pragma endregion

#pragma region /*** Definitions ***/
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

//Strips bits 5, 6
#define CTRL_KEY(k) ((k) & 0x1f)


enum editorKey{
	BACKSPACE = 127,
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
#pragma endregion

#pragma region /*** Data  ***/

typedef struct erow {
	int rsize;
	char *render;

	int size;
	char *chars;
} erow;

struct editorConfig {
	//cursor tracking
	int cx, cy;

	//rendered cursor tracking
	int rx;
	int farx;

	//screen dimensions
	int screenrows;
	int screencols;
	
	//FileText tracking
	int rowoff;
	int coloff;
	int numrows;
	erow *row;

	//editing status
	int dirty;

	//status bar
	char *filename;
	char statusmsg[80];
	time_t statusmsg_time;

	struct termios orig_termios;
};

//Global Data
struct editorConfig E;
#pragma endregion

#pragma region /*** Helper Funcs ***/
void clearScreen();
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
#pragma endregion

#pragma region /*** Terminal Functions ***/

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

//Change Terminal Attributes to enable "RAW MODE"
void enableRawMode(){
       	
	//get the initial terminal state and flags.
	if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	
	//upon exit restor to initial terminal condition
	atexit(disableRawMode); 
	
	struct termios raw = E.orig_termios; //set manipulated terminal to original

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

//Special and Reg Character Handling
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
					//Page up/down, denoted esc[5~ or esc[6~. 3 char seq
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
				//arrow key switch, denoted by esc[A 2 char seq, NSEW
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

//Locate Cursos for drawing
int getCursorPosition(int *rows, int *cols) {
	//holds returned position pair
	char buf[32];

	//length buf needs to hold?
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
#pragma endregion

#pragma region /*** output ***/
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
#pragma endregion

#pragma region /***file i/o ***/

char *editorRowsToString(int *buflen){
	int totlen = 0;
	int j;
	for (j = 0; j < E.numrows; j++) totlen += E.row[j].size + 1;
	*buflen = totlen;

	char *buf = malloc(totlen);
	char *p = buf;

	for (j = 0; j < E.numrows; j++){
		memcpy(p, E.row[j].chars, E.row[j].size);
		p += E.row[j].size;
		*p = '\n';
		p++;
	}
	return buf;
}

void editorUpdateRow(erow *row){
  	int tabs = 0;
	int j;
	for (j = 0; j < row->size; j++)
	if (row->chars[j] == '\t') tabs++;

	free(row->render);
	row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);

	int idx = 0;
	for (j = 0; j < row->size; j++){
	if (row->chars[j] == '\t') {
		row->render[idx++] = ' ';
		while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
	} else {
		row->render[idx++] = row->chars[j];
	}
	}
	row->render[idx] = '\0';
	row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len){
	if (at < 0 || at > E.numrows) return;

	E.row = realloc(E.row, ((E.numrows + 1) * sizeof(erow)));
	//line appending
	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';

	//render appending
	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	editorUpdateRow(&E.row[at]);

	E.numrows++;
	E.dirty++;
}

void editorFreeRow(erow *row) {
	free(row->render);
	free(row->chars);
}

void editorDelRow(int at){
	if(at < 0 || at >= E.numrows) return;
	editorFreeRow(&E.row[at]);
	memmove(&E.row[at], &E.row[at+1], sizeof(erow) * (E.numrows - at - 1));
	E.numrows--;
	E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c){
	if (at < 0 || at > row->size) at = row->size;
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len){
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len); //erase curr null char
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowDelChar(erow *row, int at){
	if (at < 0 || at > row->size) return;
	memmove(&row->chars[at], &row->chars[at+1], row->size - at);
	row->size--;
	editorUpdateRow(row);
	E.dirty++;
}
#pragma endregion

#pragma region //Editor Open/Save
/*
	Description:
User Input: filename
File operations: find file with name and open
Printing: Copy first line into erow.
*/
void editorOpen(char *filename){
	free(E.filename);
	E.filename = malloc(strlen(filename));
	memcpy(E.filename, filename, strlen(filename));

	//attempts to open the passed in filename
	FILE *fp = fopen(filename, "r");

	//if filename doesn't exist than throw error
	if (!fp) die("fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	
	while((linelen = getline(&line, &linecap, fp)) != -1){
		//decrement till linelen only includes characters before end of line
		while(linelen > 0 && (line[linelen - 1] == '\n' || line[linelen -1] == '\r')){
			linelen--;
			editorInsertRow(E.numrows, line, linelen);
		}
	}

	free(line);
	fclose(fp);
	E.dirty = 0;
}

void editorSave(){
	if (E.filename == NULL) return;

	int fd;
	int len;
	char *buf = editorRowsToString(&len);

	if ((fd = open(E.filename, O_CREAT | O_RDWR,  0644)) == -1) goto esEnd;
	if (ftruncate(fd, len) == -1) goto esEnd;
	if (write(fd, buf, len) != len) goto esEnd;
		close(fd);
		free(buf);
		E.dirty = 0;
	 	editorSetStatusMessage("%d bytes written to disk", len);
		return;

esEnd:
	close(fd);
	free(buf);
	E.dirty = 0;
	editorSetStatusMessage("%d bytes written to disk", len);
	return;
}
#pragma endregion

#pragma region /*** append buffer ***/
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
#pragma endregion

#pragma region//Row Operations
int editorRowCxToRx(erow *r, int cx){
	int rx = 0;
	
	if (cx == 0) {
		rx += (r->chars[0] == '\t') ? ((KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP)) + 1 : 0;
		return rx;
	} 

	for (int j = 0; j < cx; j++){
		rx += (r->chars[j] == '\t') ? ((KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP)) + 1 : 1;
	}


	return rx;
}

void editorScroll(){
	//rx settings
	E.rx = 0;
	if (E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
	}

	//vert scroll
	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	}
	if (E.cy >= E.rowoff + E.screenrows){
		E.rowoff = E.cy - E.screenrows + 1;
	}
	//horizontal scroll
	if (E.cx < E.coloff){
		E.coloff = E.rx;
	}
	if (E.cx >= E.coloff + E.screencols){
		E.coloff = E.rx - E.screencols + 1;
	}
}

void editorDrawRows(struct abuf *ab) {
	//print tildas for y rows
	int y;

	//loop through all visible rows to print
	for (y=0; y < E.screenrows; y++){

		//check if current row has an existing erow to print
		int filerow = y + E.rowoff;

		if(filerow >= E.numrows){
			//Print Centered Welcome Message at top third of editor in center
			if (E.numrows == 0 && y == E.screenrows/3){
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
			int len = E.row[filerow].rsize - E.coloff;
			if(len < 0) len = 0;
			if (len > E.screencols) len = E.screencols;
			abAppend(ab, &E.row[filerow].render[E.coloff], len);
		}
		//K erases everything to the right of the cursor, cursor moves with printing text
		abAppend(ab, "\x1b[K" ,3);
		abAppend(ab, "\r\n", 3);
	}
}
#pragma endregion

#pragma region //Editor Functions
void editorInsertChar(int c){
	//create row if one doesn't exist
	if (E.cy == E.numrows){
		editorInsertRow(E.numrows, "", 0);
	}
	editorRowInsertChar(&E.row[E.cy], E.cx, c);
	E.cx++;
}

void editorInsertNewline(){
	if (E.cx == 0){
		editorInsertRow(E.cy, "", 0);
	} else {
		erow *row = &E.row[E.cy];
		editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
		row = &E.row[E.cy];
		row->size = E.cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}
	E.cy++;
	E.cx = 0;
}

void editorDelChar(){
	//create row if one doesn't exist
	if (E.cy == E.numrows) return;
	if (E.cx == 0 && E.cy == 0) return;

	erow *row = &E.row[E.cy];
	if (E.cx > 0) {
		editorRowDelChar(&E.row[E.cy], E.cx);
		E.cx--;
	} else {
		E.cx = E.row[E.cy - 1].size;
		editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
		editorDelRow(E.cy);
		E.cy--;
	}


	
}

//any issues later on check this
//https://github.com/snaptoken/kilo-src/blob/status-bar-right/kilo.c
void editorDrawStatusBar(struct abuf *ab) {
		//invert colors
	abAppend(ab, "\x1b[7m", 4);
	//creating bar
	char status[80];
	
	int len = snprintf(status, sizeof(status), "%.20s - %d/%d - %d/%d| %d %s", E.filename ? E.filename : "[No Name]", E.cx, (E.row[E.cy].size), E.cy, E.numrows, E.dirty, E.dirty ? "(Lines Modified)" : "(clean)");

	abAppend(ab, status, ((len < E.screencols) ? len : E.screencols - len));
	while (len < E.screencols){
		abAppend(ab, " ", 1);
		len++;
	}
	
	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab){
	abAppend(ab, "\x1b[K", 3);

	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols) msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5) abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
	editorScroll();
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
	if(E.filename) editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);

	//Reposition Cursor cx, cy
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) +1);
	abAppend(&ab, buf, strlen(buf));
	
	//?25h unhides cursor
	abAppend(&ab, "\x1b[?25h", 6);
	
	//write buf out
	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...){
	va_list ap;
	va_start(ap,fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}
#pragma endregion

#pragma region /*** input ***/
void editorMoveCursor(int key){
	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

	switch (key) {
		case ARROW_UP:
			if(E.cy > 0) E.cy--;
			break;

		case ARROW_DOWN:
			if(E.cy < E.numrows) E.cy++;
			break;

		case ARROW_LEFT:
			if(E.cx > 0){
				E.cx--;
			} else if (E.cy > 0){
				E.cy --;
				E.cx = E.row[E.cy].size;
			}
			E.farx = E.cx;
			break;
		
		case ARROW_RIGHT:
			if(row && E.cx < row->size){
				E.cx++;
				
			} else if (E.cy < E.numrows){
				E.cy ++;
				E.cx = 0;
			}
			E.farx = E.cx;
			break;
	}
	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;	
	E.cx = (E.farx < rowlen) ? E.farx : rowlen;

}

void editorProcessKeypress(){
	//maintain count across calls
	static int quit_times = KILO_QUIT_TIMES;

	//get c from editor
	int c = editorReadKey();
	
	//if c is a hotkey, apply case behavior
	switch (c) {
		case '\r':
			editorInsertNewline();
			break;

		case CTRL_KEY('s'):
			editorSave();
			break;

		case CTRL_KEY('q'):
			if (E.dirty && quit_times > 0) {
				editorSetStatusMessage("WARNING!!! File has unsaved changes." "Press Ctrl-Q %d more times to quit", quit_times);
				quit_times--;
				return;
			}
			clearScreen();
			exit(0);
			break;
		
		case HOME_KEY:
			E.cx = 0;
			E.farx = E.cx;
			break;
		case END_KEY:
			E.cx = (E.cy < E.numrows) ? E.row[E.cy].size : E.cx;
			E.farx = E.cx;
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DELETE_KEY:
			if (c == DELETE_KEY) editorMoveCursor(ARROW_RIGHT);
			editorDelChar();
			break;
		
		case PAGE_UP:
		case PAGE_DOWN:
			{
				if (c == PAGE_UP){
					E.cy = E.rowoff;
				} else if (c == PAGE_DOWN){
					E.cy = E.rowoff + E.screenrows - 1;
				}

				int times = E.screenrows;
				while (times--){
					editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
				}
			}
			break;	

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);	
			break;

		case CTRL_KEY('l'):
		case '\x1b':
			break;
		
		default:
			editorInsertChar(c);
			break;
	}

	quit_times = KILO_QUIT_TIMES;
}
#pragma endregion

/*** Initialization ***/
void initEditor(){
	//init editor to def state
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;

	//offset init
	E.numrows = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.row = NULL;

	//file status
	E.dirty = 0;

	//status bar
	E.filename = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
	E.screenrows -= 2;
}

int main(int argc, char *argv[]){
	//enable editor mode
	enableRawMode();
	initEditor();

	//Checking for filename argument. no error handling yet
	if (argc >= 2){
		editorOpen(argv[1]);
	}
	
	//status message
	editorSetStatusMessage("HELP: Ctrl-Q = quit");

	//go till break
	while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}

