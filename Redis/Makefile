CC      ?= gcc
CFLAGS  ?= -std=c17 -g \
          -D_POSIX_SOURCE -D_DEFAULT_SOURCE \
          -Wall -Werror -pedantic

.PHONY: all clean

# Object files for each target
CLIENT_OBJS = client.o
SERVER_OBJS = server.o zset.o hashtable.o avltree.o

all: client server

# Executables
client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Generic rule for .c to .o
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) *.o client server
