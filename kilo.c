//Included Packages

//terminal attributes and manipulatoin

#include <termios.h>

#include <unistd.h>
#include <stdlib.h>

//packages for writing
#include <ctype.h>
#include <stdio.h>

//setting up terminal for manipulatoin

struct termios orig_termios;

void disableRawMode(){
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode(){
	tcgetattr(STDIN_FILENO, &orig_termios); //get the initial terminal state and flags.
	atexit(disableRawMode); //upon exit restor to initial terminal condition
	
	struct termios raw = orig_termios; //set manipulated terminal to original
	
	//c_iflag deals with terminal input characteristics	
	//IXON flag for data flow control, related to ctrl-s, ctrl-q
	//ICRNL turns carraige -> newline translation
	raw.c_iflag &= ~(ICRNL | IXON);
	
	//c_oflag deals with output behavior
	//OPOST flag for "\n" -> "\r\n"
	raw.c_oflag &= ~(OPOST);

	//c_lflag deals with local behavior 
	//ECHO controls if terminal shows what is typed
	//ICANON flag for canonical mode
	//IEXTEN controls extended inputs from ctrl-v and ctrl-0 for mac
	//ISIG turns off processing of signal commands, ctrl-c terminate and ctrl-z suspend
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);//set curr terminal to new flags
}



int main(){
	enableRawMode();
	
	char c;
	while(read(STDIN_FILENO, &c, 1) == 1 && c != ('q')){
		//if the character is a ctrl one print it out it's ascii val
		if(iscntrl(c)){ 
			printf("%d\r\n", c);
		} else{
			//if c not controll print out ASCII and str val
			printf("%d ('%c')\r\n", c, c); 
		}
	}
	return 0;
}
