RM=/bin/rm

# compiler/linker
CC = /usr/bin/gcc

# compiler/linker options
INCLUDE    = -I. -I /usr/local/include
LIBRARIES  = -L /usr/local/lib
C_FLAGS    = -Wall
L_FLAGS    = -levent -lpthread
CC_INCLUDE = $(INCLUDE)

C_FLAGS+= -DDEBUG_CONSOLE
#C_FLAGS+= -DDEBUG_SYSLOG


SERVER_BIN   = server
SERVER_OBJS  = server.o
SERVER_SRCS  = server.c

.c.o:
	$(CC) $(C_FLAGS) $(CC_INCLUDE) -c -g $<

all: $(SERVER_BIN)

$(SERVER_BIN) : $(SERVER_OBJS)
	$(CC) $(LIBRARIES) $(L_FLAGS) -o $(SERVER_BIN) $(SERVER_OBJS)

clean:
	$(RM) -f $(SERVER_BIN) $(SERVER_OBJS)
