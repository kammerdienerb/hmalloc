# DYLD_FORCE_FLAT_NAMESPACE=1 DYLD_INSERT_LIBRARIES="/Users/kammerdienerb/Documents/GitHub/hmalloc/lib/libhmalloc.dylib" DYLD_LIBRARY_PATH="/Users/kammerdienerb/Documents/GitHub/hmalloc/lib" ./$1
# DYLD_LIBRARY_PATH="/Users/kammerdienerb/Documents/GitHub/hmalloc/lib/" ./$1
LD_PRELOAD="/home/bkammerd/hmalloc/lib/libhmalloc.so" ./$1
