SHARED = ../shared
HEADERS = ../headers
OUT = ../out

CC = gcc
CFLAGS = -Wall
INCLUDES = -I$(HEADERS)/emulator -I$(HEADERS) -I../common

SRCS = emulator.c loader.c backend-os.c \
	$(SHARED)/diagnostics.c $(SHARED)/emSignal.c $(SHARED)/signalHandler.c $(SHARED)/sigHeap.c
TARGET = $(OUT)/ruemu


OBJS = $(SRCS:%.c=%.o)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

all: cpu ruemu

ruemu: $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

cpu:
	(cd cpu && make $@)

debug: CFLAGS += -g -O0 -DDEBUG=2
debug: ruemu


clean:
	(cd cpu && make $@)
	rm -f *.o
	rm -f $(OUT)/ruemu