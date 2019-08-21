C_FLAGS=-ansi -g -O0 -shared -fPIC -lpthread -lm -Wall -Werror -Wno-unused-function

# CC=gcc-9

all: clean lib

lib:
	mkdir -p lib
	$(CC) src/hmalloc.c $(C_FLAGS) -o lib/libhmalloc.so

check: lib

tests: clean lib
	cd test && make && ./runtest.sh test

clean:
	rm -rf lib
