CC = gcc
CFLAGS = -Wall -Wextra -Werror
LDFLAGS = -lpthread -lreadline

default: biceps

biceps: biceps.c beuip.c
	$(CC) $(CFLAGS) -o biceps biceps.c beuip.c $(LDFLAGS)

memory-leak: biceps.c beuip.c
	$(CC) -g -O0 -o biceps-memory-leaks biceps.c beuip.c $(LDFLAGS)

clean:
	rm -f biceps biceps-memory-leaks *.o