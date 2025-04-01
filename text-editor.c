#include <stdlib.h>
#include <termios.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f) // This macro takes in a letter and gets the value for CTRL+<letter>
#define TEXT_EDITOR_VERSION "0.0.1"
#define TAB_STOPS 8
#define QUIT_TIMES 3

enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DELETE_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

enum editorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRING (1<<1)

/*** data ***/
struct editorSyntax{
    char *fileType;
    char **fileMatch;
    char **keywords;
    char *singleLineCommentStart;
    char *multiLineCommentStart;
    char *multiLineCommentEnd;
    int flags;
};

typedef struct erow{
    int idx;
    int size;
    int rsize;
    char *render;
    char *chars;
    unsigned char *hl;
    int hlOpenComment;
} erow;

struct editorConfig{
    struct termios orig_termios; // Global Variable to store teh original terminal settings
    int screenRows; // Global Variable for Screen Rows
    int screenCols; // Global Variable for Screen Cols
    int cx, cy; // Global variables to keep track of the cursors position
    int rx; // Keeps track of all the invisible things renders like tabs, so if there is a tab on a line we know not to allow the cursor to go into the tab
    int numRows; // Number of rows
    erow *row; // An array of all the rows
    int rowOffset;
    int colOffset;
    int dirty; // Keeps track of if the files has been changed and not saved
    char *filename; // Keep track of filename
    char statusMsg[80]; // Message displayed at the bottom of the screen
    time_t statusMsgTime;
    struct editorSyntax *syntax;
};
struct editorConfig E;

/*** File Types ***/
char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
char *C_HL_keywords[] = {
    // Keywords 1
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case",
    // Keywrods 2
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", NULL
};
// char *TXT_HL_extensions[] = {".txt", NULL};

struct editorSyntax HLDB[] = {
    {
        "c", 
        C_HL_extensions, C_HL_keywords, 
        "//", 
        "/*",
        "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRING
    },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))


/*** prototypes ***/
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void(*callback)(char *, int));

/*** terminal ***/
void die(const char *s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode() {
    write(STDOUT_FILENO, "\x1b[2J", 4); // Clear the screen
    write(STDOUT_FILENO, "\x1b[H", 3); // Reposition the cursor to the top right
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");    // Get the original Terminal settings and store them
    atexit(disableRawMode);
    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0; // This makes it so the read function will return after everysingle byte inputted
    raw.c_cc[VTIME] = 1; // This makes it so the read function will return after 100 milliseconds if there is not byte inputted

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey() {
    int nread; // Number of bytes read 
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
      if (nread == -1 && errno != EAGAIN) die("read");
    }

    if(c == '\x1b'){
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
              if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
              if (seq[2] == '~') {
                switch (seq[1]) {
                    case '1': return HOME_KEY;
                    case '3': return DELETE_KEY;
                    case '4': return END_KEY;
                    case '5': return PAGE_UP;
                    case '6': return PAGE_DOWN;
                    case '7': return HOME_KEY;
                    case '8': return END_KEY;
                }
              }
            } else {
              switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
              }
            }
        }else if(seq[0] == 'O'){
                switch (seq[1]){
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
        }
        return '\x1b';
    }else {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    // Send the escape sequence "\x1b[6n" to request the cursor position
    // "\x1b" is the escape character, "[6n" is the command for cursor position
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    // Read the terminal's response into buf
    while (i < sizeof(buf) - 1) {
      if (read(STDIN_FILENO, &buf[i], 1) != 1) break; // Stop reading if read() fails
      if (buf[i] == 'R') break; // Stopr reading when we reach R
      i++;
    }

    if (buf[0] != '\x1b' || buf[1] != '[') return -1; // Ensure the response starts with the expected escape sequence "\x1b["
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1; // Pull the row and columns from the response string, and store them in the pointers passed in
    return 0;
}
  

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) { // The first if tries to get the window size by using ioctl
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1; // This if tries to get it by moving the cursor to bottom right anf using its position
        return getCursorPosition(rows, cols);
    } else {
      *cols = ws.ws_col;
      *rows = ws.ws_row;
      return 0;
    }
}

/*** Syntax Highlighting */
int isSeparator(int c){
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}
void editorUpdateSyntax(erow *row) {
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);
  
    if (E.syntax == NULL) return;
  
    char **keywords = E.syntax->keywords;
  
    char *scs = E.syntax->singleLineCommentStart;
    char *mcs = E.syntax->multiLineCommentStart;
    char *mce = E.syntax->multiLineCommentEnd;
  
    int scsLen = scs ? strlen(scs) : 0;
    int mcsLen = mcs ? strlen(mcs) : 0;
    int mceLen = mce ? strlen(mce) : 0;
  
    int prevSep = 1;
    int inString = 0;
    int inComment = (row->idx > 0 && E.row[row->idx - 1].hlOpenComment);
  
    int i = 0;
    while (i < row->rsize) {
      char c = row->render[i];
      unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;
  
      if (scsLen && !inString && !inComment) {
        if (!strncmp(&row->render[i], scs, scsLen)) {
          memset(&row->hl[i], HL_COMMENT, row->rsize - i);
          break;
        }
      }
  
      if (mcsLen && mceLen && !inString) {
        if (inComment) {
          row->hl[i] = HL_MLCOMMENT;
          if (!strncmp(&row->render[i], mce, mceLen)) {
            memset(&row->hl[i], HL_MLCOMMENT, mceLen);
            i += mceLen;
            inComment = 0;
            prevSep = 1;
            continue;
          } else {
            i++;
            continue;
          }
        } else if (!strncmp(&row->render[i], mcs, mcsLen)) {
          memset(&row->hl[i], HL_MLCOMMENT, mcsLen);
          i += mcsLen;
          inComment = 1;
          continue;
        }
      }
  
      if (E.syntax->flags & HL_HIGHLIGHT_STRING) {
        if (inString) {
          row->hl[i] = HL_STRING;
          if (c == '\\' && i + 1 < row->rsize) {
            row->hl[i + 1] = HL_STRING;
            i += 2;
            continue;
          }
          if (c == inString) inString = 0;
          i++;
          prevSep = 1;
          continue;
        } else {
          if (c == '"' || c == '\'') {
            inString = c;
            row->hl[i] = HL_STRING;
            i++;
            continue;
          }
        }
      }
  
      if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
        if ((isdigit(c) && (prevSep || prev_hl == HL_NUMBER)) ||(c == '.' && prev_hl == HL_NUMBER)) {
          row->hl[i] = HL_NUMBER;
          i++;
          prevSep = 0;
          continue;
        }
      }
  
      if (prevSep) {
        int j;
        for (j = 0; keywords[j]; j++) {
          int klen = strlen(keywords[j]);
          int kw2 = keywords[j][klen - 1] == '|';
          if (kw2) klen--;
  
          if (!strncmp(&row->render[i], keywords[j], klen) && isSeparator(row->render[i + klen])) {
            memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
            i += klen;
            break;
          }
        }
        if (keywords[j] != NULL) {
          prevSep = 0;
          continue;
        }
      }
  
      prevSep = isSeparator(c);
      i++;
    }
  
    int changed = (row->hlOpenComment != inComment);
    row->hlOpenComment = inComment;
    if (changed && row->idx + 1 < E.numRows)
      editorUpdateSyntax(&E.row[row->idx + 1]);
  }

int syntaxToColor(int hl){
    switch(hl){
        case HL_COMMENT:
        case HL_MLCOMMENT: return 36;
        case HL_KEYWORD1: return 33;
        case HL_KEYWORD2: return 32;
        case HL_STRING: return 35;
        case HL_NUMBER: return 31;
        case HL_MATCH: return 34;
        default: return 37;
    }
}

void editorSelectSyntaxHighlight(){
    E.syntax = NULL;

    if (!E.filename) return;
    char *ext = strrchr(E.filename, '.');
    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax *s = &HLDB[j];
        unsigned int i = 0;
        while (s->fileMatch[i]) {
            int isExt = (s->fileMatch[i][0] == '.');
            if ((isExt && ext && !strcmp(ext, s->fileMatch[i])) ||
                (!isExt && strstr(E.filename, s->fileMatch[i]))) {
                E.syntax = s;

                for(int fileRow = 0; fileRow < E.numRows; fileRow++){
                    editorUpdateSyntax(&E.row[fileRow]);
                }

                return;
            }
            i++;
        }
    }
}
/*** Row Operations ***/
int editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
      if(row->chars[j] == '\t')
        rx += (TAB_STOPS - 1) - (rx % TAB_STOPS);
      rx++;
    }
    return rx;
}

int editorRowRxToCx(erow *row, int rx) {
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++) {
      if (row->chars[cx] == '\t')
        cur_rx += (TAB_STOPS - 1) - (cur_rx % TAB_STOPS);
      cur_rx++;
      if (cur_rx > rx) return cx;
    }
    return cx;
}

void editorUpdateRow(erow *row) {
    int tabs = 0;

    for (int j = 0; j < row->size; j++)
      if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs*(TAB_STOPS-1) + 1);
    int idx = 0;
    for (int j = 0; j < row->size; j++) {
      if (row->chars[j] == '\t') {
        row->render[idx] = ' ';
        idx++;
        while (idx % TAB_STOPS != 0){
            row->render[idx++] = ' ';
        }
      } else {
        row->render[idx] = row->chars[j];
        idx++;
      }
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    editorUpdateSyntax(row);
}
void editorInsertRow(int at, char *s, size_t len){
    if(at < 0 || at > E.numRows) return;

    E.row = realloc(E.row, sizeof(erow) * (E.numRows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numRows - at));
    
    for(int j = at + 1; j <= E.numRows; j++) E.row[j].idx++;

    E.row[at].idx = at;

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    E.row[at].hlOpenComment = 0;
    editorUpdateRow(&E.row[at]);

    E.numRows++;
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c){
    if(at < 0 || at > row->size) at = row->size; // Validate the spot where we are inserting a new char
    row->chars = realloc(row->chars, row->size + 2); // Make room for the new character and the null terminator
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1); // Shift the chars to the right of teh insert down one
    row->size++;
    row->chars[at] = c; // insert the new char
    editorUpdateRow(row);  // Update the render fields, handle things like tabs
    E.dirty++;
}

void editorRowDeleteChar(erow *row, int at){
    if(at < 0 || at > row->size) at = row->size; // Validate the spot where we are inserting a mew char
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len){
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorFreeRow(erow *row){
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void editorDeleteRow(int at){
    if(at < 0 || at >= E.numRows) return;
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numRows - at - 1)); // Replace the curr row with alll the rows ahead of it
    for(int j = at; j < E.numRows - 1; j++) E.row[j].idx--;
    E.numRows--;
    E.dirty++;
}

void editorDeleteChar(){
    if(E.cy == E.numRows) return;
    if(E.cx == 0 && E.cy == 0) return;

    erow *row = &E.row[E.cy];
    if(E.cx > 0){
        editorRowDeleteChar(row, E.cx - 1);
        E.cx--;
    }else{
        E.cx = E.row[E.cy-1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDeleteRow(E.cy);
        E.cy--;
    }
}


/*** Editor Operations ***/
void editorInsertChar(int c){
    if(E.cy == E.numRows){
        editorInsertRow(E.numRows, "", 0);
    }

    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorInsertNewline() {
    if (E.cx == 0) {
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

/*** File Input/Output  ***/
char *editorRowsToString(int *bufLen){
    int totalLen = 0;
    for(int j = 0; j < E.numRows; j++){
        totalLen += E.row[j].size + 1; // Get the total length of the curr line + 1 for the \n
    }
    *bufLen = totalLen; // Store the length of the file in bufLen

    char *buf = malloc(totalLen); // Store enough spcae for the whole file
    char *p = buf; // Get a pointer that points to the beginning of the file
    for(int j = 0; j < E.numRows; j++){
        memcpy(p, E.row[j].chars, E.row[j].size); // Copy the current line to the pointer 
        p += E.row[j].size; // Move the pointer to the end of the current line
        *p =  '\n'; // Add a new line character
        p++; // Move the pointer one spot past the new line to be ready for the next line
    }

    return buf;
}

void editorSave(){
    if(E.filename == NULL){
        E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
        if(E.filename == NULL){
            editorSetStatusMessage("Save Aborted Sucessfully!");
            return;
        }
        editorSelectSyntaxHighlight();
    }

    int len;
    char *buf = editorRowsToString(&len); // Get the file contents stored in buf, and have the length of it stored in len

    int fd = (open(E.filename, O_RDWR | O_CREAT, 0644)); // Open the file with Read/Write permission, or create the file if its not there
    if(fd != -1){ // Makes sure file was opend successfully
        if(ftruncate(fd, len) != -1){ // Makes sure the memory allocation for teh file was successful
            if(write(fd, buf, len) == len){ // Makes sure the write to file was successful
                close(fd);
                free(buf);
                E.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd); // If file was opened but eveyrthing else fails, still close teh file;
    }

    free(buf); // Free the buffer holding the file anyway    
    editorSetStatusMessage("Can't Save! I/O Error: %s", strerror(errno));
}

void editorOpen(char *filename) {

    free(E.filename);  // Avoid memory leaks
    E.filename = malloc(strlen(filename) + 1);  // Allocate space for filename

    strcpy(E.filename, filename);  // Copy filename manually

    editorSelectSyntaxHighlight();

    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t lineCap = 0;
    ssize_t lineLen;

    while ((lineLen = getline(&line, &lineCap, fp)) != -1) {
      while (lineLen > 0 && (line[lineLen - 1] == '\n' || line[lineLen - 1] == '\r')) lineLen--;
      
      editorInsertRow(E.numRows, line, lineLen);
    }
    
    free(line);
    fclose(fp);
    E.dirty = 0;
  }

/** Find Function ***/
void editorFindCallback(char *query, int key) {
    static int lastMatch = -1;
    static int direction = 1;

    static int saved_hl_line;
    static char *saved_hl = NULL;
    if (saved_hl) {
        memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }

    if (key == '\r' || key == '\x1b') {
        lastMatch = -1;
        direction = 1;
        return;
    } 
    else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } 
    else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } 
    else {
        lastMatch = -1;
        direction = 1;
    }

    // If no previous match was found, start searching forward
    if (lastMatch == -1) direction = 1;

    int current = lastMatch;

    for (int i = 0; i < E.numRows; i++) {
        // Move to the next row in the search direction
        current += direction;

        if (current == -1) 
            current = E.numRows - 1;  // If at the first row, wrap to last row
        else if (current == E.numRows) 
            current = 0;  // If at the last row, wrap to first row

        erow *row = &E.row[current];

        char *match = strstr(row->render, query);
        if (match) {
            // If a match is found, update the last match index
            lastMatch = current;

            // Move the cursor to the match position
            E.cy = current;
            E.cx = editorRowRxToCx(row, match - row->render);

            // Adjust screen scrolling to ensure the match is visible
            E.rowOffset = E.numRows;

            saved_hl_line = current;
            saved_hl = malloc(row->rsize);
            memcpy(saved_hl, row->hl, row->rsize);
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

void editorFind() {
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.colOffset;
    int saved_rowoff = E.rowOffset;
    char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);
    if(query) {
        free(query);
    }else {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.colOffset = saved_coloff;
        E.rowOffset = saved_rowoff;
    }
}


/*** append buffer ***/
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
    if (len <= 0) return; // Prevent appending empty strings

    char *newBuffer = realloc(ab->b, ab->len + len);
    if (newBuffer == NULL) {
        // Handle failure properly (maybe log an error)
        return;
    }

    memcpy(&newBuffer[ab->len], s, len); // Copy new data
    ab->b = newBuffer;                   // Update pointer only after successful realloc
    ab->len += len;                       // Update length
}

void abFree(struct abuf *ab) {
    free(ab->b);
}
    
/*** output ***/
void editorScroll() {  
    E.rx = 0;
    if(E.cy < E.numRows){
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }
    // If the cursor's Y position (cy) is above the visible screen, adjust rowOffset to bring it into view  
    if (E.cy < E.rowOffset) {  
        E.rowOffset = E.cy;  
    }  
    // If the cursor's Y position is below the visible screen, scroll down to keep it visible  
    if (E.cy >= E.rowOffset + E.screenRows) {  
        E.rowOffset = E.cy - E.screenRows + 1;  
    }  

    // If the cursor's X position (cx) is left of the visible screen, adjust coloff to bring it into view  
    if (E.rx < E.colOffset) {  
        E.colOffset = E.rx;  
    }  
    // If the cursor's X position is beyond the visible screen width, scroll right to keep it visible  
    if (E.rx >= E.colOffset + E.screenCols) {  
        E.colOffset = E.rx - E.screenCols + 1;  
    }  
}  



void editorDrawRows(struct abuf *ab){
    for(int y = 0; y < E.screenRows; y++) {
        int fileRow = y + E.rowOffset;
        if(fileRow >= E.numRows){      
            if(E.numRows == 0 &&  y == E.screenRows/3){
                char welcome[80];
                int welcomeLen = snprintf(welcome, sizeof(welcome), "Text Editor -- version %s", TEXT_EDITOR_VERSION);
                if (welcomeLen > E.screenCols) welcomeLen = E.screenCols; // If screen too narrow, only print out the part of the message that will fit
                int padding  = (E.screenCols - welcomeLen)/2;
                // If there is padding to center the version, make sure to still add the ~ to show row
                if(padding){
                    abAppend(ab, "~", 1);
                    padding--; // Decrease amount of padding needed by one since we added the ~
                }
                while(padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomeLen);
            }else{
                abAppend(ab, "~", 1);
            }
        }else{
            int len = E.row[fileRow].rsize - E.colOffset;
            if(len < 0) len = 0;
            if(len > E.screenCols) len = E.screenCols;
            char *c = &E.row[fileRow].render[E.colOffset];
            unsigned char *hl = &E.row[fileRow].hl[E.colOffset];
            int currentColor = -1;
            for(int j = 0; j < len; j++){
                if(iscntrl(c[j])){
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                    abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, &sym, 1);
                    abAppend(ab, "\x1b[m", 3);	
                    if(currentColor != -1){
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", currentColor);
                        abAppend(ab, buf, clen);
                    }
                }else if(hl[j] == HL_NORMAL) {
                    if(currentColor != -1){
                        abAppend(ab, "\x1b[39m", 5);
                        currentColor = -1;
                    }                    
                    abAppend(ab, &c[j], 1);
                }else {
                    int color = syntaxToColor(hl[j]);
                    if(color != currentColor){
                        currentColor = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abAppend(ab, buf, clen);
                    }
                    abAppend(ab, &c[j], 1);
                }
            }
            abAppend(ab, "\x1b[39m]", 5);
        }
        abAppend(ab, "\x1b[K", 3);
        // Add a new line as ling as we are not at the bottom of the screen
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4); // Change color to inverted
    char status[80], rstatus[80]; // File Info for status bar
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename ? E.filename : "[No Name]", E.numRows, E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", E.syntax ? E.syntax->fileType : "No File Type", E.cy+1, E.numRows);
    if(len > E.screenCols) len = E.screenCols;
    abAppend(ab, status, len); // Print out the Filename and num of lines on the left side of bar
    while (len < E.screenCols) { // Print rest inverted color status bar
        if(E.screenCols - len == rlen){
            abAppend(ab, rstatus, rlen);
            break;
        }else{
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3); // Change color back to normal
    abAppend(ab, "\r\n", 2); // Make room for the second status bar with the Status Msg
}

void editorDrawMessageBar(struct abuf *ab){
    abAppend(ab, "\x1b[K", 3); // Clear the message bar
    int msgLen = strlen(E.statusMsg);
    if (msgLen > E.screenCols) msgLen = E.screenCols;
    if (msgLen && time(NULL) - E.statusMsgTime < 5) abAppend(ab, E.statusMsg, msgLen);
}

void editorRefreshScreen() {
    editorScroll();
    
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3); // Reposition the cursor to the top right
  
    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    // Place the cursor on teh screen based off its current position
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowOffset) + 1, (E.rx - E.colOffset) + 1);
    abAppend(&ab, buf, strlen(buf));    
    
    abAppend(&ab, "\x1b[?25l", 6);

    abAppend(&ab, "\x1b[?25h", 6); // Put the cursor back

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusMsg, sizeof(E.statusMsg), fmt, ap);
    va_end(ap);
    E.statusMsgTime = time(NULL);
  }

/*** input ***/
char *editorPrompt(char *prompt, void(*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
  
    size_t buflen = 0;
    buf[0] = '\0';
  
    while (1) {
      editorSetStatusMessage(prompt, buf);
      editorRefreshScreen();
  
      int c = editorReadKey();
      if (c == DELETE_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
        if (buflen != 0) buf[--buflen] = '\0';
      } else if (c == '\x1b') {
        editorSetStatusMessage("");
        if(callback) callback(buf, c);
        free(buf);
        return NULL;
      } else if (c == '\r') {
        if (buflen != 0) {
          editorSetStatusMessage("");
          if(callback) callback(buf, c);
          return buf;
        }
      } else if (!iscntrl(c) && c < 128) {
        if (buflen == bufsize - 1) {
          bufsize *= 2;
          buf = realloc(buf, bufsize);
        }
        buf[buflen++] = c;
        buf[buflen] = '\0';
      }
      if(callback) callback(buf, c);

    }
  }

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];

    switch (key) {
      case ARROW_LEFT:
        if(E.cx != 0){
            E.cx--;
        }else if(E.cy != 0){
            E.cy--;
            E.cx = E.row[E.cy].size;
        }
        break;
      case ARROW_RIGHT:
        if(row && E.cx < row->size){
            E.cx++;
        }else if(row && E.cx == row->size){
            E.cy++;
            E.cx = 0;
        }
        break;
      case ARROW_UP:
        if(E.cy != 0) E.cy--;
        break;
      case ARROW_DOWN:
        if(E.cy != E.numRows) E.cy++;
        break;
    }


    
    row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];
    // Get the length of current row, and if the cursor is past it, snap it to the end of current line
    int rowLen = row ? row->size : 0;
    if (E.cx > rowLen) {
        E.cx = rowLen;
    }
}
void editorProcessKeypress() {
    static int quit_times = QUIT_TIMES;
    int c = editorReadKey();
    switch (c) {
        case '\r': // Enter Key
            editorInsertNewline();
            break;
        case CTRL_KEY('q'):
            if(E.dirty && quit_times > 0){
                editorSetStatusMessage("WARNING! File has unsaved changes. Press Ctrl-Q %d more times to quit", quit_times);
                quit_times--;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4); // Clear the screen
            write(STDOUT_FILENO, "\x1b[H", 3); // Reposition the cursor to the top right
            exit(0);
            break;
        case CTRL_KEY('s'):
            editorSave();
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            if(E.cy < E.numRows) E.cx = E.row[E.cy].size;
            break;
        case CTRL_KEY('f'):
            editorFind();
            break;
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DELETE_KEY:
            if(c == DELETE_KEY) editorMoveCursor(ARROW_RIGHT); 
            editorDeleteChar();
            break;
        case PAGE_UP:
        case PAGE_DOWN:
        {
            if (c == PAGE_UP) {
              E.cy = E.rowOffset;
            } else if (c == PAGE_DOWN) {
              E.cy = E.rowOffset + E.screenRows - 1;
              if (E.cy > E.numRows) E.cy = E.numRows;
            }
            int times = E.screenRows;
            while (times--)
              editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
        case CTRL_KEY('l'): // Do nothing on this because screen auto refreshes
        case '\x1b': // Dont want other escape sequences ebing typed
            break
;
        default:
            editorInsertChar(c);
            break;
    }

    quit_times = QUIT_TIMES;
}

/*** init ***/

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.numRows = 0;
    E.row = NULL;
    E.rowOffset = 0;
    E.colOffset = 0;
    E.filename = NULL;
    E.statusMsg[0] = '\0';
    E.statusMsgTime = 0;
    E.dirty = 0;
    E.syntax = NULL;
    if (getWindowSize(&E.screenRows, &E.screenCols) == -1) die("getWindowSize"); // Get the window size
    E.screenRows -= 2; // Make room for the status bar
}

int main(int argc, char*argv[]){
    enableRawMode();
    initEditor();
    if(argc >= 2){
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    while(1){
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
