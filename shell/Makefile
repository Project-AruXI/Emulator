SHARED = ../shared
HEADERS = ../headers
OUT = ../out

CC = gcc
CFLAGS = -Wall
INCLUDES = -I$(HEADERS)/shell -I$(HEADERS)

SRCS = shell.c  environment.c parser.c io.c $(SHARED)/signalHandler.c $(SHARED)/emSignal.c $(SHARED)/diagnostics.c \
	$(SHARED)/sigHeap.c
TARGET = $(OUT)/ash


OBJS = $(SRCS:%.c=%.o)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

all: shell

shell: $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

debug: CFLAGS += -g -O0 -DDEBUG=2
debug: shell

clean:
	rm -f *.o
	rm -f $(SHARED)/*.o
	rm -f $(OUT)/ash