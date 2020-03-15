#ifndef JOTE_VERSION
/* DEFINES */
#define JOTE_VERSION "0.0.1"

#define TAB_SIZE 4
#define QUIT_TIMES 4

#define CTRL_KEY(k) ((k) & 0x1f)

enum EditorKey {
	BACKSPACE = 127,
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
	int isTextModified;
	EditorRow *row;
	char *fileName;
	char statusMessage[80];
	time_t statusMessage_time;
	struct termios original_termios;
};

struct EditorConfig editor;


/* APPEND BUFFER */
struct EditorBuffer {
	char *buffer;
	int length;
};

#define EDITOR_BUFFER_INIT {NULL, 0};

#endif
