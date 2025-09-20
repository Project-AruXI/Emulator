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
	(cd build-utils && make $@)
	(cd emulator && make $@)
	(cd shell && make $@)

test:
	(cd testsuite/assembler && go test)


clean:
	(cd emulator && make $@)
	(cd shell && make $@)

clean_logs:
	rm -f out/*.debug
	rm -f out/*.log