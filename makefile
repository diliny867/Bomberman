
main: main.c server.c server.h config.h client.c client.h
	gcc main.c server.c client.c -o main -std=c99 -Wall -Wno-pointer-sign -Wno-unused-variable
/*CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Wno-pointer-sign -Wno-unused-variable

SRC = main.c server.c client.c
OBJ = $(SRC:.c=.o)

TARGET = main

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)*/
