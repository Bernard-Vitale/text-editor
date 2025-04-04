CC = gcc
CFLAGS = -g -Wall -Wextra -pedantic -std=c99
TARGET = text-editor
SRC = text-editor.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
