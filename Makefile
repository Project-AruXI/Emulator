CC = gcc
CFLAGS = -Wall


all: ruemu cpu shell


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