
main: main.c server.c server.h config.h
	gcc main.c server.c -o main -std=c99 -Wall -Wno-pointer-sign -Wno-unused-variable