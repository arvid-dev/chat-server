CC = gcc

SERVER = chat-server
CLIENT = chat-client
OBJS = my_netlib.o chat-server.o chat-client.o
SRCS = $(OBJS:%.o=%.c)
CFLAGS = -g -std=gnu99 -W -Wall
LDFLAGS =

all: $(SERVER) $(CLIENT)

$(SERVER): my_netlib.o chat-server.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(SERVER) my_netlib.o chat-server.o
	$(LDFLAGS)

$(CLIENT): my_netlib.o chat-client.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(CLIENT) my_netlib.o chat-client.o $(LDFLAGS)

clean:
	@rm -f *.o $(SERVER) $(CLIENT)

.PHONY: clean
