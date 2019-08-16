C_FLAGS=-ansi -g -O0 -dynamiclib -fPIC -Wall -Werror -Wno-unused-function

# CC=gcc-9

all: clean lib

lib:
	mkdir -p lib
	$(CC) src/hmalloc.c $(C_FLAGS) -o lib/libhmalloc.dylib

check: lib

tests: clean lib
	cd test && make && ./runtest.sh test

clean:
	rm -rf lib
