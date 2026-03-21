CC = gcc
CFLAGS = -Wall
COMMON_LIBDIR = ./common/lib

all: ruemu cpu shell


commonlibs:
	$(MAKE) -C $(COMMON_LIBDIR) libargparse

ruemu:
	(cd emulator && make $@)

cpu:
	(cd emulator/cpu && make $@)

shell:
	(cd shell && make $@)

debug:
	(cd emulator && make $@)
	(cd shell && make $@)

clean:
	(cd emulator && make $@)
	(cd shell && make $@)

clean_logs:
	rm -f out/*.debug
	rm -f out/*.log