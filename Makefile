CFG_DEB1=-g -O0 -DHMALLOC_DO_ASSERTIONS -DHMALLOC_DO_LOGGING
CFG_DEB2=-g -O0 -DHMALLOC_DO_LOGGING
CFG_DEB3=-g -O0
CFG_REL1=-O3 -DHMALLOC_DO_ASSERTIONS
CFG_REL2=-g -O3
CFG_REL3=-O3

CFG=$(CFG_DEB1)
# CFG=$(CFG_DEB2)
# CFG=$(CFG_DEB3)
# CFG=$(CFG_REL1)
# CFG=$(CFG_REL2)
# CFG=$(CFG_REL3)

C_FLAGS=-shared -fPIC -lpthread -lm -Wall -fmax-errors=3 -Werror -Wno-unused-function $(CFG)

# CC=gcc-9

all: the_lib

the_lib:
	@mkdir -p lib
	$(CC) src/hmalloc.c $(C_FLAGS) -o lib/libhmalloc.so

check: clean the_lib

tests: clean the_lib
	cd test && make && ./runtest.sh test && ./runtest.sh test_pp && ./runtest.sh test_thr

clean:
	rm -rf lib
