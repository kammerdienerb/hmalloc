CFG_DEB=-g -O0 -DHMALLOC_DO_ASSERTIONS -DHMALLOC_DO_LOGGING
CFG_REL=-O3

CFG=$(CFG_DEB)
# CFG=$(CFG_REL)

C_FLAGS=-ansi -shared -fPIC -lpthread -lm -Wall -Werror -Wno-unused-function $(CFG)

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
