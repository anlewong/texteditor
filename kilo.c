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


#define CTRL_KEY(k) ((k) & 0x1f) //Strips bits 5, 6

enum editorKey //enumurate special keys
{
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

typedef struct erow //stores editor row data
{
	//rendered 'visible' vars
	int rsize;
	char *render;

	//internal rep
	int size;
	char *chars;

	//Styling vars
	unsigned char *hl;
} erow;

struct editorConfig {
	//cursor tracking
	int cx, cy;

	//rendered 'visible' cursor tracking
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
char *editorPrompt(char *prompt, void (*callback)(char *, int));
int editorRowRXtoCX(erow *r, int rx);

#pragma endregion

#pragma region /*** Terminal Functions ***/

void die(const char*s) //handle & print errors
{
	clearScreen(); //clear screen
	perror(s); //Prints s as an error message
	exit(1); //returns as fail to execute succesfully
}


void disableRawMode() //return Terminal to initial settings
{
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr"); //set terminal to saved original state
}


void enableRawMode()//Change Terminal Attributes to enable "RAW MODE"
{
	if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr"); //save initial terminal state (flags etc.). Error if fail
	atexit(disableRawMode); //Restor initial lterminal condition when code terminated
	struct termios raw = E.orig_termios; //set manipulated terminal to original
	/*Input Flag Explanations
	  IRCN toggles '\r\n' rendering
	  IXON toggles data output via ctrl keys
	  BRKINT toggles sigInterrupt on break
	  INPCK toggles parity check
	  ISTRIP toggles 8th bit strip*/
	raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
	/* Output Flag Explanations 
	   OPOST flag for "\n" -> "\r\n"
	*/
	raw.c_oflag &= ~(OPOST);
	/*  Character Flag
		CS8 is bitmask so all chars are 8 bits per byte	
	*/
	raw.c_cflag |= (CS8);
	/* Local Behavior Flag Explanations
	c_lflag deals with local behavior 
	ECHO controls if terminal shows what is typed
	ICANON flag for canonical mode
	IEXTEN controls extended inputs from ctrl-v and ctrl-0 for mac
	ISIG turns off processing of signal commands, ctrl-c terminate and ctrl-z suspend
	*/
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	/* Terminal Timeout Behavior
	VMIN - sets minum bytes for read before return to 0
	VTIME - sets read wait to 1/10 of a second
	*/
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;	
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); //Set terminal to modified 'raw' state
}

int editorReadKey() //input handling
{
	int nread; //bytes read
	char c; //curr char read
	//Spin lock till c is valid character
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) //spin lock till valid character
	{
		if (nread == -1 && errno != EAGAIN) die("read"); //Ignore Timeout, Error Handling
	}
	if(c == '\x1b') //c is Escape Seq
	{
		char seq[3]; //max special command length
		
		//3 > valid command >= 2
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b'; 
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
		if (seq[0] == '[') //[%d~ logic
		{
			if (seq[1] >= '0' && seq[1] <= '9') //Command req 3rd input
			{
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b'; //No read No Command
				if (seq[2] == '~')//Command struct [%d~, [0-9]
				{
					switch (seq[1]) //command -> key
					{
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
		} else if (seq[0] == 'O') //special command logic O%s, [H,F]
		{
			switch (seq[1]){
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}

		return '\x1b'; //No command Return Escape Char
	} 
	return c; //Non-Command char
}

int getCursorPosition(int *rows, int *cols) //Get Cursor Posiition in Window | Pass back rowXcol posiiton
{
	char buf[32]; //Buffer to hold system coordinate pair response
	unsigned int i = 0; //buffer length var
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1; //Ask system for cursors pos
	while(read(STDIN_FILENO, &buf[i], 1) == 1) //read system response into buf
	{
		if(buf[i] == 'R') break; //break on last char of coordinate pair
		i++; //increment message length
	}	
	buf[i] = '\0'; //set last char of buf to string terminator
	if(buf[0] != '\x1b' || buf[1] != '[') return -1; //validate that sys response is a command seq
 	if(sscanf(&buf[2], "%d;%d", rows, cols) == -2) return -1; //validate coordinate pair & pass row, col values to parent via input arg pointers
	return 0; //Row/Col passed up through *rows, *cols.
}
#pragma endregion

#pragma region /*** output ***/
void clearScreen(){
	write(STDOUT_FILENO, "\x1B[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
}


int getWindowSize(int *rows, int *cols) //grab window size from os, pass back hxw or rxc
{
	//Window size struct holds cols and rows
	struct winsize ws; //os structure that holds height (row) and width (col)

	//ioctl - input output control,(fd, op, &stuct);
	//TIOCGWINSZ - request window size of file
	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) //req window size from system, returns true if auto req fail
	{
		if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1; //C foward, B down, move cursor to bottom left
		return getCursorPosition(rows, cols); //use cursor position in bottom left to find rowsxcols
	} else {
		*cols = ws.ws_col; //grab columns from winsize object
		*rows = ws.ws_row; //grab rows from winsize object
		return 0;
	}
}
#pragma endregion

#pragma region /***file i/o ***/

char *editorRowsToString(int *buflen) //Editor representation -> Buf
{
	int totlen = 0; //buf length var
	int i; //loop counter
	for (i = 0; i < E.numrows; i++) totlen += E.row[i].size + 1; //iterate through all rows, sum length of all rows
	*buflen = totlen; //set buf length to totlen

	char *buf = malloc(totlen); //assign buf enough memory to hold all row data
	char *p = buf; //local var to manipulate buf which will be passed up to parent

	for (i = 0; i < E.numrows; i++) //iterate through all rows
	{
		memcpy(p, E.row[i].chars, E.row[i].size); //copy curr row to buf
		p += E.row[i].size; //increment p index to end of curr row/line
		*p = '\n'; //append '\n' to seperate written lines
		p++; //increment p to nexxt free index.
	}
	return buf; //return buffer containing all informatoin held by editor
}

void editorUpdateRow(erow *row) //creates 'visible'/'rendered' formatted rows
{
  	int tabs = 0; //tab counter
	int i; //loop counter
	for (i = 0; i < row->size; i++) //loop through all chars in row
	if (row->chars[i] == '\t') tabs++; //count tabs in row
	free(row->render); //free frs
	row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1); //allocate '\0', tabs (8), chars (1)ea.
	int idx = 0; //render string index var
	for (i = 0; i < row->size; i++) //iterate through all char in row
	{
	if (row->chars[i] == '\t') //check char is tab
	{
		row->render[idx++] = ' '; //'render' 8 total spaces for tab
		while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' '; //7 tab space renderer & idx incrementer
	} else {
		row->render[idx++] = row->chars[i]; //'render' char increment idx
	}
	}
	row->render[idx] = '\0';//terminate 'rendered' string
	row->rsize = idx; //'render' size = last 'render' index
}

void editorInsertRow(int at, char *s, size_t len) //create and insert erow
{
	if (at < 0 || at > E.numrows) return; //return if currRow outside allocated row range [0-numrows]

	E.row = realloc(E.row, ((E.numrows + 1) * sizeof(erow))); //give Editor row pointer space to point to new erow
	memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at)); //open up gap @ at for new erow
	E.row[at].size = len; //set new erow's internal len
	E.row[at].chars = malloc(len + 1); //allocate new erows internal string (is)
	memcpy(E.row[at].chars, s, len); //copy string S to is.
	E.row[at].chars[len] = '\0'; //set is[len] -> '\0'
	E.row[at].rsize = 0; //set new rows render size
	E.row[at].render = NULL; //no rendering applied to row yet
	E.row[at].hl = NULL; //no stylization applied to row yet
	editorUpdateRow(&E.row[at]); //updates the row
	E.numrows++; //trakc new num of rows
	E.dirty++; //track num of edits made
}

void editorFreeRow(erow *row) //free row struct/object
{
	free(row->render); //free 'visible' format string representation or FRS
	free(row->chars); //free 'internal' string representation isr.
	free(row->hl); //free styling string
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

//Editor Open/Save
/* Description: User Input: filename File operations: find file with name and open Printing: Copy first line into erow.*/
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
		}
		editorInsertRow(E.numrows, line, linelen);
	}

	free(line);
	fclose(fp);
	E.dirty = 0;
}

void editorSave(){
	if (E.filename == NULL) {		
		if ((E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL)) == NULL) {
			editorSetStatusMessage("Save Aborted");
			return;
		}
	} 

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

#pragma region //Find
void editorFindCallback(char *query, int key) {
	static int last_match = -1;
	static int direction = 1;

	//set's our direction and curr match
	if (key == '\r' || key == '\x1b') {
		last_match = -1;
		direction = 1;
		return;
	} else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1; 
	} else if (key == ARROW_LEFT || key == ARROW_UP) {
		direction = -1; 
	} else {
		last_match = -1;
		direction = 1;
	}

	//sets up curr match
	if (last_match == -1) direction = 1;
	int current = last_match;
	int i;
	for (i = 0; i < E.numrows; i++){
		current += direction;
		if (current == -1) current = E.numrows -1;
		else if (current == E.numrows) current = 0;

		erow *row = &E.row[current];
		char *match = strstr(row->render, query);
		if (match) {
			last_match = current;
			E.cy = current;
			E.cx = editorRowRXtoCX(row, match - row->render);
			E.rowoff = E.numrows;
			break;
		}

	}

}

void editorFind(){
	int saved_cx = E.cx;
	int saved_cy = E.cy;
	int saved_coloff = E.coloff;
	int saved_rowoff = E.rowoff;

	char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);
	if (query){
		free(query);
	} else {
		E.cx = saved_cx;
		E.cy = saved_cy;
		E.coloff = saved_coloff;
		E.rowoff = saved_rowoff;
	}
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

	for (int i = 0; i < cx; i++){
		rx += (r->chars[i] == '\t') ? ((KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP)) + 1 : 1;
	}


	return rx;
}

int editorRowRXtoCX(erow *r, int rx){
	int cur_rx = 0;
	int cx;

	for (cx = 0; cx < r->size; cx++){
		if (r->chars[cx] == '\t') cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
	
		cur_rx++;
		
		if (cur_rx > rx) return cx;
	}

	return cx;
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
	int y; //counter var for loops
	for (y=0; y < E.screenrows; y++) //loop through local 'visible' rows
	{
		int filerow = y + E.rowoff; //Global row
		if(filerow >= E.numrows) //>= Allocatd Rows
		{
			if (E.numrows == 0 && y == E.screenrows/3) //Check Empty file, Position 1/3 from top
			{
				char welcome[80]; //MessageBuf
				int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo Editor -- version %s", KILO_VERSION); //Editor Version Message, Records Length
				if (welcomelen > E.screencols) welcomelen = E.screencols; //Truncate len to 'visible' local columns
				int padding = (E.screencols - welcomelen)/2; //Find Distance to Middle of screen
				//Place Cursor in middle of screen
				if (padding){
					abAppend(ab, "~", 1);
					padding--;
				}
				while(padding--) abAppend(ab, " ", 1);	

				abAppend(ab, welcome, welcomelen); //Print Welcome Message
			} else {
				abAppend(ab, "~", 1); //Tilda Empty Lines	
			}
		} else //Global Row within allocated rows
		{
			
			int len = E.row[filerow].rsize - E.coloff; //set length to available columns
			if(len < 0) len = 0; //If len < 0, enough columns for whole message
			if (len > E.screencols) len = E.screencols; //If len > E.screencols, truncate lenght to just num of columns			
			char *c = &E.row[filerow].render[E.coloff]; //Points to current row's render string
				int j; //loop variable
				for (j = 0; j < len; j++) //loop through formatted render string (frs)
				{
					if (isdigit(c[j]))//check if frs[j] = [0-9]
					{
						abAppend(ab, "\x1b[31m", 5); //font color: curr -> red
						abAppend(ab, &c[j], 1);//append red number char
						abAppend(ab, "\x1b[39m", 5); //font color: red -> def
					} else {
						abAppend(ab, &c[j], 1);//append white non-number char
					}
				}
		}
		abAppend(ab, "\x1b[K" ,3); //Erase right of cursor
		abAppend(ab, "\r\n", 3); //Newline
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
	if (E.cy == E.numrows){
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
	if (E.cx == 0 && E.cy == 0) {
		editorRowDelChar(&E.row[E.cy], E.cx);
		return;
	}
	

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

	abAppend(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];

	int len = snprintf(status, sizeof(status), "%.20s - %d/%d C - %d/%d R | %d %s",
		E.filename ? E.filename : "[No Name]", E.cx, (E.row) ? E.row[E.cy].size : 0,  
		E.cy, E.numrows,  E.dirty, E.dirty ? "(Lines Modified)" : "(clean)");

	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);

	if (len > E.screencols) len = E.screencols;
	abAppend(ab, status, len);
	while (len < E.screencols) {
		if (E.screencols - len == rlen) {
			abAppend(ab, rstatus, rlen);
		break;
		} else {
			abAppend(ab, " ", 1);
			len++;
		}
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
char *editorPrompt(char *prompt, void(*callback)(char *, int)){
	size_t bufsize = 128;
	char *buf = malloc(bufsize);

	size_t buflen = 0;
	buf[0] = '\0';

	while (1) {
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();

		int c = editorReadKey();
		if (c == DELETE_KEY || c == CTRL_KEY('h') || c == BACKSPACE){
			if (buflen != 0) buf [--buflen] = '\0';
		} else if (c == '\x1b') {
			editorSetStatusMessage("");
			if (callback) callback(buf, c);
			free(buf);
			return NULL;
		} else if (c == '\r'){
			if(buflen != 0){
				editorSetStatusMessage("");
				if (callback) callback(buf, c);
				return buf;
			}
		} else if (!iscntrl(c) && c < 128){
			if(buflen == bufsize -1){
				bufsize *= 2;
				buf = realloc(buf, bufsize);
			}
			buf[buflen++] = c;
			buf[buflen] = '\0';
		}
		if (callback) callback(buf, c);
	}
}

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
			
		case CTRL_KEY('f'):
			editorFind();
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
			if (E.cx != 0 || E.cy != 0){
					int startx = E.cx;
					editorMoveCursor(ARROW_LEFT);
					editorDelChar();
					if (startx > 1){
						editorMoveCursor(ARROW_RIGHT);
					} 
					
				} 
				break;
		case DELETE_KEY:
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
	editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

	//go till break
	while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}

