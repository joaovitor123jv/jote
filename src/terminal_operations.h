/* INCLUDES */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "structs.h"

/* TERMINAL */
void die(const char *string) {
	write(STDOUT_FILENO, "\x1b[2J", 4); // Clear the entire screen
	write(STDOUT_FILENO, "\x1b[H", 3); // Position the cursor on top of the screen

	perror(string); // Print the error
	exit(1);
}

void disableRawMode() {
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &editor.original_termios) == -1) {
		die("tcsetattr");
	}
}

void enableRawMode() {
	struct termios raw;
	if(tcgetattr(STDIN_FILENO, &editor.original_termios) == -1) {
		die("tcgetattr");
	}
	atexit(disableRawMode);

	raw = editor.original_termios;
	// ICANON = Canonical mode flag
	// ISIG = Signals flag (SIGINT, SIGSTP)
	// ECHO = "Print each typed character on screen" flag
	// IXON = Flag to software-flow control with ctrl-s and ctrl-q combinations (like vim)
	// IEXTEN = Flag of default behavior of ctrl-v combination
	// ICRNL = Flag that says "when user input carriage return, create a new line" (Carriage Return -> New Line)
	// BRKINT = When te BRKINT is set on, a break condition will cause a SIGINT signal to be sent to program
	// INPCK = enables parity checking, which doesn’t seem to apply to modern terminal emulators.
	// ISTRIP = Causes the 8th bit of each input byte to be stripped, meaning it will set it to 0.
	raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
	raw.c_oflag &= ~(OPOST); // Disables default behavior of carriage return creating a new line
	raw.c_cflag |= (CS8); // Ensures that the character size is 8 bits per byte
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw.c_cc[VMIN] = 0; // Number of bytes "read" must read before return
	raw.c_cc[VTIME] = 1; // Read interval to 100 miliseconds

	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
		die("tcsetattr");
	}
}

int editorReadKey() {
	int nread;
	char character;
	while((nread = read(STDIN_FILENO, &character, 1) != 1)) {
		if(nread == -1 && errno != EAGAIN) {
			die("read");
		}
	}

	if(character == '\x1b') {
		char sequence[3];

		if(read(STDIN_FILENO, &sequence[0], 1) != 1) {
			return '\x1b';
		}
		if(read(STDIN_FILENO, &sequence[1], 1) != 1) {
			return '\x1b';
		}

		if(sequence[0] == '[') {
			if(sequence[1] >= '0' && sequence[1] <= '9') {
				if(read(STDIN_FILENO, &sequence[2], 1) != 1) {
					return '\x1b';
				}
				if(sequence[2] == '~') {
					switch(sequence[1]) {
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			} else {
				switch(sequence[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		} else if(sequence[0] == 'O') {
			switch(sequence[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}

		return '\x1b';
	} else {
		return character;
	}
}

int getCursorPosition(int *rows, int *cols) {
	char buffer[32];
	unsigned int i = 0;

	if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
		return -1;
	} else {
		// printf("\r\n");
		// while(read(STDIN_FILENO, &character, 1) == 1) {
		while(i < sizeof(buffer) - 1) {
			if(read(STDIN_FILENO, &buffer[i], 1) != 1) {
				break;
			} else if(buffer[i] == 'R') {
				break;
			}

			i++;
		}

		buffer[i] = '\0';

		if(buffer[0] != '\x1b' || buffer[1] != '[') {
			return -1;
		}
		if(sscanf(&buffer[2], "%d;%d", rows, cols) != 2) {
			return -1;
		}

		return 0;
	}
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;

	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
			return -1;
		}
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}
