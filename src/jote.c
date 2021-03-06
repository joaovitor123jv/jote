#define _GNU_SOURCE
#define _BSD_SOURCE
#define _DEFAULT_SOURCE

/* INCLUDES */
#include <ctype.h>
#include <errno.h>

#include <fcntl.h>

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

#include "terminal_operations.h"

/* PROTOTYPES */

void editorSetStatusMessage(const char *format, ...);

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
	editor.isTextModified = 1;
}

void editorRowInsertCharacter(EditorRow *row, int at, int character) {
	if(at < 0 || at > row->size) {
		at = row->size;
	}
	
	row->characters = realloc(row->characters, row->size + 2);
	memmove(&row->characters[at + 1], &row->characters[at], row->size - at + 1);
	row->size++;
	row->characters[at] = character;
	editorUpdateRow(row);
	editor.isTextModified = 1;
}

/* EDITOR OPERATIONS */
void editorInsertCharacter(int character) {
	if(editor.cursorY == editor.numRows) {
		editorAppendRow("", 0);
	}
	editorRowInsertCharacter(&editor.row[editor.cursorY], editor.cursorX, character);
	editor.cursorX++;
}

/* FILE I/O */

char *editorRowsToString(int *bufferLength) {
	int textLength = 0;
	int iterator;
	
	// count the size of the future file text
	for(iterator = 0; iterator < editor.numRows; iterator++) {
		textLength += editor.row[iterator].size + 1; // +1 cause of the \n at the line end
	}
	*bufferLength = textLength;
	
	char *buffer = malloc(textLength);
	char *p = buffer;
	
	// Copy the editor buffer text to another buffer adding '\n' at the end of each line
	for(iterator = 0; iterator < editor.numRows; iterator++) {
		memcpy(p, editor.row[iterator].characters, editor.row[iterator].size);
		p += editor.row[iterator].size;
		*p = '\n';
		p++;
	}
	
	return buffer;
}

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

	while((lineLength = getline(&line, &lineCap, fp)) != -1) {
		while(lineLength > 0 && (line[lineLength - 1] == '\n' || line[lineLength - 1] == '\r')) {
			lineLength--;
		}
		editorAppendRow(line, lineLength);
	}

	free(line);
	fclose(fp);
	editor.isTextModified = 0;
}

void editorSave() {
	if(editor.fileName == NULL) {
		return;
	} else {
		int length;
		char *buffer = editorRowsToString(&length);
		
		int fd = open(editor.fileName, O_RDWR | O_CREAT, 0644);
		
		if(fd != -1) {
			if(ftruncate(fd, length) != -1) {
				if(write(fd, buffer, length) == length) {
					close(fd);
					free(buffer);
					editor.isTextModified = 0;
					editorSetStatusMessage("%d bytes written to the file", length);
					return;
				}
			}
			close(fd);
		}
		free(buffer);
		editorSetStatusMessage("Can\'t save! I/O error: %s", strerror(errno));
	}
}

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

	length = snprintf(
		status, 
		sizeof(status), 
		"%.20s - %d lines %s", 
		editor.fileName ? editor.fileName : "<New File>", 
		editor.numRows,
		editor.isTextModified ? "(modified)" : ""
	);
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
	static int quitTimes = QUIT_TIMES;
	int character = editorReadKey();

	switch(character) {
		case '\r':
			/* TODO */
			break;
	
		case CTRL_KEY('q'):
			if(editor.isTextModified && quitTimes > 0) {
				editorSetStatusMessage("WARNING: There are unsaved changes in file"
					"Press Ctrl-Q %d more times to quit.", quitTimes
				);
				quitTimes--;
				return;
			}
			write(STDOUT_FILENO, "\x1b[2J", 4); // Clear the entire screen
			write(STDOUT_FILENO, "\x1b[H", 3); // Position the cursor on top of the screen
			exit(0);
			break;

		case CTRL_KEY('s'):
			editorSave();
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

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			/* TODO */
			break;

		case CTRL_KEY('l'):
		case '\x1b':
			break;

		default:
			editorInsertCharacter(character);
			break;
	}
	
	quitTimes = QUIT_TIMES;
}


/* INIT */
void initEditor() {
	editor.cursorX = 0;
	editor.cursorY = 0;
	editor.renderedX = 0;
	editor.rowOffset = 0;
	editor.colOffset = 0;
	editor.numRows = 0;
	editor.isTextModified = 0;
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

	editorSetStatusMessage("Help: Press Ctrl-Q to quit | Ctrl-S to save");

	while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
}
