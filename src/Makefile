CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -pthread
LDFLAGS =

SRC = sensor.c user_console.c input_validation.c system_manager.c write_log.c
OBJ = $(SRC:.c=.o)
HEADER = dados.h

all: home_iot sensor user_console

home_iot: system_manager.o write_log.o
	$(CC) $(CFLAGS) $^ -o $@

sensor: $(OBJ)
	$(CC) $(CFLAGS) sensor.c input_validation.o -o $@

user_console: $(OBJ)
	$(CC) $(CFLAGS) user_console.c -o $@

%.o: %.c $(HEADER)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) home_iot sensor user_console
