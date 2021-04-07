CFG_DEB1=-g -O0 -DHMALLOC_DEBUG -DHMALLOC_DO_ASSERTIONS -DHMALLOC_DO_LOGGING
CFG_DEB2=-g -O0 -DHMALLOC_DEBUG -DHMALLOC_DO_ASSERTIONS
CFG_DEB3=-g -O0 -DHMALLOC_DEBUG -DHMALLOC_DO_LOGGING
CFG_DEB4=-g -O0 -DHMALLOC_DEBUG
CFG_REL1=-march=native -mtune=native -O3 -DHMALLOC_DO_ASSERTIONS
CFG_REL2=-march=native -mtune=native -g -O3
CFG_REL3=-march=native -mtune=native -O3
CFG_REL_LOG=-march=native -mtune=native -O3 -DHMALLOC_DO_LOGGING

# CFG=$(CFG_DEB1)
# CFG=$(CFG_DEB2)
# CFG=$(CFG_DEB3)
# CFG=$(CFG_DEB4)
# CFG=$(CFG_REL1)
# CFG=$(CFG_REL2)
# CFG=$(CFG_REL3)
CFG=$(CFG_REL_LOG)

MAX_ERRS=-fmax-errors=3

# FP=-fno-omit-frame-pointer # enable for profiling/debugging
LTO=-flto

C_FLAGS=-c -fPIC $(LTO) $(FP) -ftls-model=initial-exec -Wall $(MAX_ERRS) -Werror -Wno-unused-function -Wno-unused-value $(CFG)
CXX_FLAGS=$(C_FLAGS) -fno-rtti
LD_FLAGS=$(LTO) -ftls-model=initial-exec -fno-rtti -lpthread -lm -ldl -lrt

# CC=gcc-9

all: the_lib

the_lib:
	@mkdir -p lib
	$(CC)  src/hmalloc.c $(C_FLAGS) -o lib/libhmalloc.o
	$(CXX) src/hmalloc_cpp.cpp $(C_FLAGS) -o lib/libhmalloc_cpp.o
	$(CXX) -shared lib/libhmalloc.o lib/libhmalloc_cpp.o $(LD_FLAGS) -o lib/libhmalloc.so

check: clean the_lib

tests: clean the_lib
	cd test && make && ./runtest.sh test && ./runtest.sh test_pp && ./runtest.sh user_heap

clean:
	rm -rf lib
