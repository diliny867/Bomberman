
main: main.c server.c client.c config.c client.h server.h config.h 
	gcc main.c server.c client.c config.c -o main -std=c99 -Wall -Wno-pointer-sign -Wno-unused-variable -Wno-unused-but-set-variable