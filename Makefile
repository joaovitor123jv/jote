CC=gcc
WARNINGS=-Wall -Wextra
STD=-std=c99

jote: src/jote.c
	$(CC) $^ -o $@ $(WARNINGS) -pedantic $(STD)
