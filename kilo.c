/*
dd
d
d
d

*/

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

enum editorHighlight //string class coloring
{
	HL_NORMAL = 0,
	HL_COMMENT,
	HL_MLCOMMENT,
	HL_KEYWORD1,
	HL_KEYWORD2,
	HL_STRING,
	HL_NUMBER,
	HL_MATCH //for searches/find
};

#define HL_HIGHLIGHT_NUMBERS (1<<0) //bit flag num hl
#define HL_HIGHLIGHT_STRINGS (1<<1) //bit flag string hl

#pragma endregion

#pragma region /*** Data  ***/

struct editorSyntax {
	char *filetype; //stores filetype extension
	char **filematch; //array of formats to search through
	char **keywords;
	char *singeline_comment_start;

	char **multiline_comment;
	int flags; //bit field turn on and off diff hl
};

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

	//syntax settings
	struct editorSyntax *syntax;

	//terminal settings
	struct termios orig_termios;
};

//Global Data
struct editorConfig E; //editor object/struct

char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL}; //list of supported filetype extensions
char *C_HL_keywords[] = {"switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case", "#define|", "#include|",
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL};
char *C_HL_comments[] = {"/*", "*/", NULL};

struct editorSyntax HLDB[] = //DB of HL params based on filetype
{
	 //C syntax entry, name c, supports extensions found in second param, highlight numbers on.
	{ 
		"c",
		C_HL_extensions,
		C_HL_keywords,
		"//",
		C_HL_comments,
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
	},
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0])) //HLDB entry size = total size/ size of 1 entry

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

void clearScreen() //erases terminal screen
{
	write(STDOUT_FILENO, "\x1B[2J", 4); //Erase Entire Screen
	write(STDOUT_FILENO, "\x1b[H", 3); //Place Cursor Top Left
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

#pragma region /***Syntax Highlighting ***/

int is_seperator(int c) //checks if c is a seperating character
{
	return isspace(c) || c == '\0' || strchr(",.()+=/*=~%%<>[];", c); //checks if char is a space, end of line (eol) or in string def last
}

void editorUpdateSyntax(erow *row) //update styling string for a row
{
	row->hl = realloc(row->hl, row->rsize); //allocate HL enough mem to store encodings for entire rendered string
	memset(row->hl, HL_NORMAL, row->rsize); //Iniitally set HL to have every char normal color

	if (E.syntax == NULL) return; //no HL guide so leave normal

	char **keywords = E.syntax->keywords;

	char *scs = E.syntax->singeline_comment_start; //grabas the scs char
	int scs_len = scs ? strlen(scs) : 0; //sets the len of our scs char
	
	char *mcs = E.syntax->multiline_comment[0]; //mlcs char
	int mcs_len = mcs ? strlen(mcs) : 0; //sets the len of our mlcs char
	char *mce = E.syntax->multiline_comment[1]; //mce char
	int mce_len = mce ? strlen(mce) : 0; //sets the len of our mce char

	int prev_sep = 1; //starts as true every line
	int in_string = 0; //mark start of string
	static int in_comment = 0;
	

	int i = 0;
	while(i < row->rsize){
		char c = row->render[i]; //char to check
		unsigned char prev_hl = (i > 0) ? row->hl[i-1] : HL_NORMAL; //index last char if possible

		if (scs_len && !in_string && !in_comment) //checks that we have a scs char and our outside a string
		{
			if (!strncmp(&row->render[i], scs, scs_len)) //checks curr pos if it's a scs
			{
				memset(&row->hl[i], HL_COMMENT, row->rsize - i); //sets row from scs on to common color
				break;
			}
		}

		if (mcs_len && mce_len && !in_string){
			if (in_comment){
					row->hl[i] = HL_MLCOMMENT;
					if (!strncmp(&row->render[i], mce, mce_len)){
						memset(&row->hl[i], HL_MLCOMMENT, mce_len);
						i += mce_len;
						in_comment = 0; //check if mlce char
						prev_sep = 1;
						continue;
					} else {
						i++;
						continue;
					}
						
			} else if (!strncmp(&row->render[i], mcs, mcs_len)){
					memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
					i+= mcs_len;
					in_comment = 1;
					continue;
				}
		}

		if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) //check string flag
		{
			if (in_string){ //curr in string
				row->hl[i] = HL_STRING; //hl string
				
				if (c == '\\' && i + 1 < row->rsize) { //handle \" and \' withiin a string
					row->hl[i+1] = HL_STRING;
					i+=2;
					continue;
				}

				if (c == in_string) in_string = 0; //hit " or ' so end of string
				i++; //increment
				prev_sep = 1; //sep stay true
				continue;

			} else {
				if (c == '"' || c == '\'') {
					in_string = c; //string equals " or ' in ascii
					row->hl[i] = HL_STRING; //color quote
					i++; //increment
					continue;
				}
			}
		}


		if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) //check if num hl enabled
		{
			if (isdigit(c) && (prev_sep || prev_hl == HL_NUMBER || //Check C is number and prev char is either sep or num
				(c == '.' && prev_hl == HL_NUMBER))) //allow decimals, hl . 
			{
				row->hl[i] = HL_NUMBER; //If curr rendered char is num, indicate number coloring in HL styling string
				i++; //increment i
				prev_sep = 0; //curr hl so no sep
				continue; //go to next char
			}			
		}
		
		if (prev_sep) {
			int j;
			for (j = 0; keywords[j]; j++) //iterate through all keywords
			{
				int klen = strlen(keywords[j]); //lenght of curr keyword check
				int kw2 = keywords[j][klen-1] == '|'; //specify kword type based on last char
				if(kw2) klen--; //remove kw2 symbol

				if (!strncmp(&row->render[i], keywords[j], klen) //check curr index start of keyword
				&& is_seperator(row->render[i+klen])){ //confirm keyword has sep at end
					memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen); //set kw color based on kw2 marker
					i+= klen; //iterate past kw and post sep
					break; //can't continue since it would hit inner loop
				}
			}
			if (keywords[j] != NULL) //hl a keyword
			{
				prev_sep = 0;//curr char is end of kw and not sep
				continue;
			}
		}
		
		prev_sep = is_seperator(c); //update prev_sep tracker
		i++; //increment i to iterate through row
	}
}

int editorSyntaxToColor(int hl) //return ASCII color code given HL spec 
{
	switch (hl){
		case HL_COMMENT: 
		case HL_MLCOMMENT: return 36; //cyan
		case HL_KEYWORD1: return 33; //yellow
		case HL_KEYWORD2: return 32; //green
		case HL_STRING: return 35; //magenta
		case HL_NUMBER: return 31; //red
		case HL_MATCH: return 34; //blue
		default: return 37; //def: white
	}
}

void editorSelectSyntaxHighlight() //searches for HLDB entry based on filetype
{
	E.syntax = NULL; //set syntax to null,setting new syntax based on result
	if (E.filename == NULL) return; //no filename

	char *ext = strrchr(E.filename, '.'); //grab pointer to last .
	for (unsigned int j = 0; j < HLDB_ENTRIES; j++) //iterate through hldb entries
	{
		struct editorSyntax *s = &HLDB[j]; //local copy of curr hldb entry
		unsigned int i = 0; 
		while (s->filematch[i]) //iterate through entrie's accepted filetypes
		{
			int is_ext = (s->filematch[i][0] == '.'); //makes sure ext has a .
			if ((is_ext && ext && !strcmp(ext, s->filematch[i])) //if curr filematch is ext, ext eists, and is match
			|| (!is_ext && strstr(E.filename, s->filematch[i])) //currfilematch isn't an extension, but filename is a substring of supported filetype
			) {
				E.syntax = s; //set syntax to matching entry

				int filerow;
				for (filerow = 0; filerow < E.numrows; filerow++)//iterate through all rows
				{
					editorUpdateSyntax(&E.row[filerow]); //update all rows to match hl guide
				}
				return;
			}
			i++; //inrement to next filetype
		}
		
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

#pragma region //Row Operations

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

void editorUpdateRow(erow *row) //updates 'rendered' row and highlight scheme
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

	editorUpdateSyntax(row); //create highlight scheme for row
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
			unsigned char *hl = &E.row[filerow].hl[E.coloff]; //Pointer to Current Row's HL scheme
			int current_color = -1; //basic color mem var
			int j; //loop variable
			for (j = 0; j < len; j++) //loop through formatted render string (frs)
			{
				if (iscntrl(c[j])) //cntrl char processing
				{
					char sym = (c[j] <= 26) ? '@' + c[j] : '?'; //render cntrl char as @A etc
					abAppend(ab, "\x1b[7m", 4); //invert color
					abAppend(ab, &sym, 1);
					abAppend(ab, "\x1b[m", 3); //def color
					if (current_color != -1) //non def color
					{
						char buf[16];
						int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color); //set back to formatted color
						abAppend(ab, buf, clen);
					}


				} else if (hl[j] == HL_NORMAL) //normal hl
				{
					if(current_color != -1) //check if curr color not default
					{
						abAppend(ab, "\x1b[39m", 5); //set output to def color
						current_color = -1;
					}
					abAppend(ab, &c[j], 1); //append curr char
				} else //custom hl
				{
					int color = editorSyntaxToColor(hl[j]); //get hl encoding
					if (color != current_color) //is curr color new color?
					{
						current_color = color; //set curr color to new color
						char buf[16]; //hold set color command
						int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color); //set buf to color command
						abAppend(ab, buf, clen); //set color to HL code.
					}
					abAppend(ab, &c[j], 1); //append curr char
				}
			}
			abAppend(ab, "\x1b[39m", 5); //set output to def color
		}
		abAppend(ab, "\x1b[K" ,3); //Erase right of cursor
		abAppend(ab, "\r\n", 3); //Newline
	}
}

#pragma endregion

#pragma region //Editor Functions

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
void editorDrawStatusBar(struct abuf *ab) //status bar curr line/row, filetype, filename etc.
{

	abAppend(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];//Left and Right corners of status bar

	int len = snprintf(status, sizeof(status), "%.20s - %d, %d| %s | %d %s",
		E.filename ? E.filename : "[No Name]",//write filename if exist
		E.cx, //curr col
		E.cy, //curr row
		E.syntax ? E.syntax->filetype : "what?",
		E.dirty, //num changes 
		E.dirty ? "(Lines Modified)" : "(clean)"); 

	int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", 
	E.syntax ? E.syntax->filetype : "no ft", //display filetype if it exists
	E.cy + 1, //curr visible row
	E.numrows); //total rows

	if (len > E.screencols) len = E.screencols; //print only vis col
	abAppend(ab, status, len); //print status
	while (len < E.screencols) { //if space
		if (E.screencols - len == rlen) { //Write what's left of visible space
			abAppend(ab, rstatus, rlen); //print rendered status
		break;
		} else {
			abAppend(ab, " ", 1); //space to pad
			len++;
		}
	}
	
	abAppend(ab, "\x1b[m", 3); //uninvert colors
	abAppend(ab, "\r\n", 2); //newline
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

#pragma region /***file i/o ***/
//Editor Open/Save
/* Description: User Input: filename File operations: find file with name and open Printing: Copy first line into erow.*/
void editorOpen(char *filename){
	free(E.filename); //ensure blank filename to rewrite
	//tried strdup but it seg faulted
	E.filename = malloc(strlen(filename)); //allocate new mem to filename
	memcpy(E.filename, filename, strlen(filename)); //copy filename into editor object
	editorSelectSyntaxHighlight(); //setup syntax HL for file 

	FILE *fp = fopen(filename, "r"); //attempts to open the passed in filename
	if (!fp) die("fopen"); //if filename doesn't exist than throw error

	char *line = NULL; //line holder var
	size_t linecap = 0; //max amount to readin
	ssize_t linelen; //length read into line
	
	while((linelen = getline(&line, &linecap, fp)) != -1){
		//decrement till linelen only includes characters before end of line
		while(linelen > 0 //make sure line has content
		&& (line[linelen - 1] == '\n' //curchar not a newline
		|| line[linelen -1] == '\r')) //curchar not a return 
		{
			linelen--; //decrement index till not \n\r
		}
		editorInsertRow(E.numrows, line, linelen); //insert written row into numrows object
	}

	free(line); //free line holding var
	fclose(fp); //close file
	E.dirty = 0; //set dirty flags to 0 since file just opened
}

void editorSave(){
	if (E.filename == NULL) //If filename null go here
	{		
		if ((E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL)) == NULL) //prompt them to enter filename 
		{
			editorSetStatusMessage("Save Aborted"); //no filename so no save
			return;
		}
		editorSelectSyntaxHighlight();
	} 

	int fd; 
	int len;
	char *buf = editorRowsToString(&len); //big buf to store all content from editor

	if ((fd = open(E.filename, O_CREAT | O_RDWR,  0644)) == -1) goto esEnd; //open file, create if doesn't exist
	if (ftruncate(fd, len) == -1) goto esEnd; //truncate to length of content needed to write
	if (write(fd, buf, len) != len) goto esEnd; //write all content from buf onto file
	close(fd); 
	free(buf);
	E.dirty = 0; //no more dirty flags
	editorSetStatusMessage("%d bytes written to disk", len); //closing message
	return;

esEnd:
	close(fd);
	free(buf);
	E.dirty = 0;
	editorSetStatusMessage("%d bytes written to disk", len); //closing message
	return;
}

#pragma endregion

#pragma region //Find
void editorFindCallback(char *query, int key) {
	static int last_match = -1; //last match maintain throuhgout calls
	static int direction = 1; //search dir  maintain throuhgout calls
	
	static int saved_hl_line; //track higlighted match line
	static char *saved_hl = NULL; //Store original line settings

	if (saved_hl)  // a saved hl exists
	{
		memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize); //copy original settings back into row
		free(saved_hl); //free string so no trigger next time;
		saved_hl = NULL; //reset to null.
	}


	//set's our direction and curr match
	if (key == '\r' || key == '\x1b') //exits search if escape/enter pressed
	{
		last_match = -1;
		direction = 1;
		return;
	} else if (key == ARROW_RIGHT || key == ARROW_DOWN) //next match
	{
		direction = 1; 
	} else if (key == ARROW_LEFT || key == ARROW_UP) //prev match
	{
		direction = -1; 
	} else //maintain match
	{
		last_match = -1; 
		direction = 1;
	}

	if (last_match == -1) direction = 1; //reset direction to 1
	int current = last_match; //set current match
	int i; //loop counter
	for (i = 0; i < E.numrows; i++) //iterate through rows
	{
		current += direction; //go to next match
		if (current == -1) current = E.numrows -1; //if last match go to first match
		else if (current == E.numrows) current = 0; //if first match go to last match

		erow *row = &E.row[current]; //temp row for internal use
		char *match = strstr(row->render, query); //check row for matches and return string
		if (match) //match found
		{
			last_match = current; //set last_match to curr
			E.cy = current; //update cursor position to point to match
			E.cx = editorRowRXtoCX(row, match - row->render); //convert rx to cx and move mouse to match
			E.rowoff = E.numrows; //position match at top of editor
			
			saved_hl_line = current; //saving match line
			saved_hl = malloc (row->rsize); //allocate 'render' space
			memcpy(saved_hl, row->hl, row->rsize); //save original 'rendered' hl scheme
			memset(&row->hl[E.cx], HL_MATCH, strlen(query)); //set match color to blue
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

	//syntax settings
	E.syntax = NULL;

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
	//editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

	//go till break
	while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}

