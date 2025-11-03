SHARED = ../shared
COMMON = ../common
HEADERS = ../headers
OUT = ../out

CC = gcc
CFLAGS = -Wall
INCLUDES = -I$(HEADERS)/emulator -I$(HEADERS)/emulator/cpu -I$(HEADERS) -I$(COMMON)

SRCS = emulator.c loader.c backend-os.c \
	$(SHARED)/diagnostics.c $(SHARED)/emSignal.c $(SHARED)/signalHandler.c $(SHARED)/sigHeap.c
LIBS = $(COMMON)/libargparse.a
TARGET = $(OUT)/ruemu


OBJS = $(SRCS:%.c=%.o)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

all: cpu ruemu

ruemu: $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

cpu:
	(cd cpu && make $@)

debug: CFLAGS += -g -O0 -DDEBUG=2
debug: ruemu


clean:
	rm -f *.o
	rm -f $(OUT)/ruemu

cpu_clean:
	(cd cpu && make $@)