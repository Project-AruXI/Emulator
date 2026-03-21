SHARED = ../shared
COMMON = ../common
HEADERS = ../headers
COMMON_LIBDIR = $(COMMON)/lib
OUT = ../out

CC = gcc
CFLAGS = -Wall
INCLUDES = -I$(HEADERS)/emulator -I$(HEADERS)/emulator/cpu -I$(HEADERS) -I$(COMMON)/defs/instr -I$(COMMON)/defs

SRCS = emulator.c loader.c loader-reloc.c backend-os.c \
	$(SHARED)/diagnostics.c $(SHARED)/emSignal.c $(SHARED)/signalHandler.c $(SHARED)/sigHeap.c
LIBS = $(COMMON_LIBDIR)/libargparse.a
TARGET = $(OUT)/ruemu


OBJS = $(SRCS:%.c=%.o)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

all: cpu ruemu

commonlibs:
	$(MAKE) -C $(COMMON_LIBDIR) libargparse

ruemu: $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

cpu:
	(cd cpu && make $@)

debug: CFLAGS += -g -O0 -DDEBUG=3
debug: ruemu


clean:
	rm -f *.o
	rm -f $(OUT)/ruemu

cpu_clean:
	(cd cpu && make $@)