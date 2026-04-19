
main: main.c server.c server.h config.h client.c client.h
	gcc main.c server.c client.c -o main -std=c99 -Wall -Wno-pointer-sign -Wno-unused-variable