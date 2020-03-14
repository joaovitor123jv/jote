#define _GNU_SOURCE
#define _BSD_SOURCE
#define _DEFAULT_SOURCE

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

/* DEFINES */
#define JOTE_VERSION "0.0.1"

#define TAB_SIZE 4

#define CTRL_KEY(k) ((k) & 0x1f)

enum EditorKey {
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

/* DATA */

typedef struct EditorRow {
	int size;
	int renderSize;
	char *characters;
	char *render;
} EditorRow;

struct EditorConfig {
	int cursorX;
	int cursorY;
	int renderedX;
	int rowOffset;
	int colOffset;
	int screenRows;
	int screenCols;
	int numRows;
	EditorRow *row;
	char *fileName;
	char statusMessage[80];
	time_t statusMessage_time;
	struct termios original_termios;
};

struct EditorConfig editor;

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

/* ROW OPERATIONS */
int calculateRenderedX(EditorRow *row, int cursorX) {
	int renderedX = 0;
	int iterator;
	for(iterator = 0; iterator < cursorX; iterator++) {
		if(row->characters[iterator] == '\t') {
			renderedX += (TAB_SIZE - 1) - (renderedX % TAB_SIZE);
		}
		renderedX++;
	}

	return renderedX;
}

void editorUpdateRow(EditorRow *row) {
	int tabs = 0;
	int iterator;
	int index = 0;

	// Count the amount of tabs
	for(iterator = 0; iterator < row->size; iterator++) {
		if(row->characters[iterator] == '\t') {
			tabs++;
		}
	}

	free(row->render);
	row->render = malloc(row->size + (tabs * (TAB_SIZE-1)) + 1);

	for(iterator = 0; iterator < row->size; iterator++) {
		if(row->characters[iterator] == '\t') {
			row->render[index++] = ' ';
			while((index % TAB_SIZE) != 0) {
				row->render[index++] = ' ';
			}
		} else {
			row->render[index++] = row->characters[iterator];
		}
	}

	row->render[index] = '\0';
	row->renderSize = index;
}

void editorAppendRow(char *line, size_t lineLength) {
	// int at = editor.numRows;
	editor.row = realloc(editor.row, sizeof(EditorRow) * (editor.numRows + 1));

	editor.row[editor.numRows].size = lineLength;
	editor.row[editor.numRows].characters = malloc(lineLength + 1); // Includes the space to '\0'
	memcpy(editor.row[editor.numRows].characters, line, lineLength);
	editor.row[editor.numRows].characters[lineLength] = '\0';

	editor.row[editor.numRows].renderSize = 0;
	editor.row[editor.numRows].render = NULL;
	editorUpdateRow(&editor.row[editor.numRows]);

	editor.numRows++;
}

/* FILE I/O */

void editorOpen(const char *fileName) {
	FILE *fp = fopen(fileName, "r");
	char *line = NULL;
	size_t lineCap = 0;
	ssize_t lineLength;

	if(!fp) {
		die("fopen");
	}

	free(editor.fileName);
	editor.fileName = strdup(fileName);

	// lineLength = getline(&line, &lineCap, fp);

	// if(lineLength != 1) {
	while((lineLength = getline(&line, &lineCap, fp)) != -1) {
		while(lineLength > 0 && (line[lineLength - 1] == '\n' || line[lineLength - 1] == '\r')) {
			lineLength--;
		}
		editorAppendRow(line, lineLength);
	}
	// }

	free(line);
	fclose(fp);
}

/* APPEND BUFFER */
struct EditorBuffer {
	char *buffer;
	int length;
};

#define EDITOR_BUFFER_INIT {NULL, 0};

void editorBuffer_append(struct EditorBuffer *eb, const char *string, int length) {
	char *new = realloc(eb->buffer, eb->length + length);

	if(new == NULL) {
		return;
	} else {
		memcpy(&new[eb->length], string, length);
		eb->buffer = new;
		eb->length += length;
	}
}

void editorBuffer_free(struct EditorBuffer *eb) {
	free(eb->buffer);
}

/* OUTPUT */
void enableNegativeMode(struct EditorBuffer *eb) {
	editorBuffer_append(eb, "\x1b[7m", 4);
}

void defaultMode(struct EditorBuffer *eb) {
	editorBuffer_append(eb, "\x1b[m", 3);
}

void editorScroll() {
	editor.renderedX = 0;
	if(editor.cursorY < editor.numRows) {
		editor.renderedX = calculateRenderedX(&editor.row[editor.cursorY], editor.cursorX);
	}

	// Vertical scrolling
	if(editor.cursorY < editor.rowOffset) {
		editor.rowOffset = editor.cursorY;
	}
	if(editor.cursorY >= (editor.rowOffset + editor.screenRows)) {
		editor.rowOffset = editor.cursorY - editor.screenRows + 1;
	}

	// Horizontal scrolling
	if(editor.renderedX < editor.colOffset) {
		editor.colOffset = editor.renderedX;
	}
	if(editor.renderedX >= editor.colOffset + editor.screenCols) {
		editor.colOffset = editor.renderedX - editor.screenCols + 1;
	}
}

void editorDrawRows(struct EditorBuffer *eb) {
	int y = 0;
	int length = 0;
	int fileRow = 0;

	for(y = 0; y < editor.screenRows; y++) {
		fileRow = y + editor.rowOffset;
		if(fileRow >= editor.numRows) {
			if((editor.numRows == 0) && (y == editor.screenRows / 2)) {
				char welcome[80];
				int welcomeLength = snprintf(welcome, sizeof(welcome), "Welcome to JoTE Editor ==> Version: %s", JOTE_VERSION);
				if(welcomeLength > editor.screenCols) {
					welcomeLength = editor.screenCols;
				}

				int padding = ((editor.screenCols - welcomeLength) / 2) - 1;
				editorBuffer_append(eb, "~", 1);

				while(padding--) {
					editorBuffer_append(eb, " ", 1);
				}

				editorBuffer_append(eb, welcome, welcomeLength);
			} else {
				editorBuffer_append(eb, "~", 1); // Add a '~' at each line beginning of buffer
			}
		} else {
			length = (editor.row[fileRow].renderSize - editor.colOffset);
			if(length < 0) {
				length = 0;
			}
			if(length > editor.screenCols) {
				length = editor.screenCols;
			}
			editorBuffer_append(eb, &editor.row[fileRow].render[editor.colOffset], length);
		}
		editorBuffer_append(eb, "\x1b[K", 3); // Erases current line to the right of the cursor

		// if(y < editor.screenRows - 1) {
			editorBuffer_append(eb, "\r\n", 2);
		// }
	}
}

void editorDrawStatusBar(struct EditorBuffer *eb) {
	int length = 0;
	int rightLength = 0;
	char status[80];
	char rightStatus[80];

	length = snprintf(status, sizeof(status), "%.20s - %d lines", editor.fileName ? editor.fileName : "<New File>", editor.numRows);
	rightLength = snprintf(rightStatus, sizeof(rightStatus), "(%d,%d)", editor.cursorX + 1, editor.cursorY + 1);

	if(length > editor.screenCols) {
		length = editor.screenCols;
	}

	enableNegativeMode(eb);

	editorBuffer_append(eb, status, length);

	while(length < editor.screenCols) {
		if(editor.screenCols - length == rightLength) {
			editorBuffer_append(eb, rightStatus, rightLength);
			break;
		} else {
			editorBuffer_append(eb, " ", 1);
			length++;
		}
	}
	defaultMode(eb);

	editorBuffer_append(eb, "\r\n", 2);
}

void editorDrawMessageBar(struct EditorBuffer *eb) {
	editorBuffer_append(eb, "\x1b[K", 3);
	int messageLength = strlen(editor.statusMessage);
	if(messageLength > editor.screenCols) {
		messageLength = editor.screenCols;
	}

	if(messageLength && (time(NULL) - editor.statusMessage_time < 5)) {
		editorBuffer_append(eb, editor.statusMessage, messageLength);
	}
}

void editorRefreshScreen() {
	struct EditorBuffer eb = EDITOR_BUFFER_INIT;
	char buffer[32];

	editorScroll();
	editorBuffer_append(&eb, "\x1b[?25l", 6); // Hides cursor
	// editorBuffer_append(&eb, "\x1b[2J", 4); // Clear the entire screen
	editorBuffer_append(&eb, "\x1b[H", 3); // Reposition cursor at the screen top

	editorDrawRows(&eb);
	editorDrawStatusBar(&eb);
	editorDrawMessageBar(&eb);

	snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", 
			(editor.cursorY - editor.rowOffset) + 1, 
			(editor.renderedX - editor.colOffset) + 1);

	editorBuffer_append(&eb, buffer, strlen(buffer)); // Reposition cursor at the last cursor position
	editorBuffer_append(&eb, "\x1b[?25h", 6); // Show cursor

	write(STDOUT_FILENO, eb.buffer, eb.length);
	editorBuffer_free(&eb);
}

void editorSetStatusMessage(const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(editor.statusMessage, sizeof(editor.statusMessage), format, arguments);
	va_end(arguments);
	editor.statusMessage_time = time(NULL);
}

/* INPUT */

void editorMoveCursor(int key) {
	int rowLength;
	EditorRow *row = (editor.cursorY >= editor.numRows) ? NULL : &editor.row[editor.cursorY];

	switch(key) {
		case ARROW_LEFT:
			if(editor.cursorX != 0) {
				editor.cursorX--;
			} else if(editor.cursorY > 0) {
				editor.cursorY--;
				editor.cursorX = editor.row[editor.cursorY].size;
			}
			break;
		case ARROW_RIGHT:
			if(row && editor.cursorX < row->size) {
				editor.cursorX++;
			} else if(row && editor.cursorX == row->size) {
				editor.cursorY++;
				editor.cursorX = 0;
			}
			break;
		case ARROW_UP:
			if(editor.cursorY != 0) {
				editor.cursorY--;
			}
			break;
		case ARROW_DOWN:
			if(editor.cursorY < editor.numRows) {
				editor.cursorY++;
			}
			break;
	}

	row = (editor.cursorY >= editor.numRows) ? NULL : &editor.row[editor.cursorY];
	rowLength = row ? row->size : 0;
	if(editor.cursorX > rowLength) {
		editor.cursorX = rowLength;
	}
}

void editorProcessKeypress() {
	int character = editorReadKey();

	switch(character) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4); // Clear the entire screen
			write(STDOUT_FILENO, "\x1b[H", 3); // Position the cursor on top of the screen
			exit(0);
			break;

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(character);
			break;
		case PAGE_DOWN:
		case PAGE_UP:
			{
				if(character == PAGE_UP) {
					editor.cursorY = editor.rowOffset;
				} else if(character == PAGE_DOWN) {
					editor.cursorY = editor.rowOffset + editor.screenRows - 1;
					if(editor.cursorY > editor.numRows) {
						editor.cursorY = editor.numRows;
					}
				}

				int times = editor.screenRows;
				while(times--) {
					editorMoveCursor(character == PAGE_UP ? ARROW_UP : ARROW_DOWN);
				}
			}
			break;

		case HOME_KEY:
			editor.cursorX = 0;
			break;

		case END_KEY:
			if(editor.cursorY < editor.numRows) {
				editor.cursorX = editor.row[editor.cursorY].size;
			}
			break;
	}
}


/* INIT */
void initEditor() {
	editor.cursorX = 0;
	editor.cursorY = 0;
	editor.renderedX = 0;
	editor.rowOffset = 0;
	editor.colOffset = 0;
	editor.numRows = 0;
	editor.row = NULL;
	editor.fileName = NULL;
	editor.statusMessage[0] = '\0';
	editor.statusMessage_time = 0;

	if(getWindowSize(&editor.screenRows, &editor.screenCols) == -1) {
		die("getWindowSize");
	}
	editor.screenRows -= 2; // Disable last line for text editing
}

int main(int argc, char *argv[]) {
	enableRawMode();
	initEditor();

	if(argc >= 2) {
		editorOpen(argv[1]);
	}

	editorSetStatusMessage("Help: Press Ctrl-Q to quit the editor");

	while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
		/*
		   if(iscntrl(character)) { // Is a non-printable caracter ?
		   printf("%d\r\n", character);
		   } else { // Is a printable character
		   printf("%d ('%c')\r\n", character, character);
		   }
		   */
	}
}
