SHARED = ../../shared
HEADERS = ../../headers
OUT = ../../out

CC = gcc
CFLAGS = -Wall -lpthread
INCLUDES = -I$(HEADERS)/emulator/cpu -I$(HEADERS)/emulator -I$(HEADERS)

SRCS = cpu.c core.c mem.c hardware.c $(SHARED)/emSignal.c $(SHARED)/diagnostics.c \
	$(SHARED)/signalHandler.c $(SHARED)/sigHeap.c
TARGET = $(OUT)/iaru0

# ifeq ($(MAKECMDGOALS),windows)
# SRCS := $(SRCS) getline.c
# TARGET = ../out/arxsm.exe
# endif

OBJS = $(SRCS:%.c=%.o)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

all: cpu

cpu: $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

debug: CFLAGS += -g -O0 -DDEBUG=2
debug: cpu

# lib: $(SRCS)
# $(CC) -fPIC -shared -o ../out/libarxsm.so $(SRCS) $(INCLUDES)

clean:
	rm -f *.o
	rm -f $(SHARED)/*.o
	rm -f $(OUT)/iaru0