# Some configuration options
#----------------------------

# Any extra options you need
EXTRADEFS=

# For GCC the C++11 support also needs to be enabled explicitly
override DEFS+=-std=c++11

# This may help or hurt performance. Try it and see for yourself.
override DEFS+=-fomit-frame-pointer

# By default iqdb uses integer math for the similarity computation,
# because it is often slightly faster than floating point math
# (and iqdb cannot make use of SSE et.al.) You can remove this option
# if you wish to compare both versions. This setting has
# negligible impact on the value of the similarity result.
override DEFS+=-DINTMATH

# -------------------------
#  no configuration below
# -------------------------

.SUFFIXES:

all:	iqdb

.PHONY: clean

clean:
	rm -f *.o iqdb

%.o : %.h
%.o : %.cpp
iqdb.o : imgdb.h haar.h auto_clean.h debug.h
imgdb.o : imgdb.h imglib.h haar.h auto_clean.h delta_queue.h debug.h
test-db.o : imgdb.h delta_queue.h debug.h
haar.o :
%.le.o : %.h
iqdb.le.o : imgdb.h haar.h auto_clean.h debug.h
imgdb.le.o : imgdb.h imglib.h haar.h auto_clean.h delta_queue.h debug.h
haar.le.o :

.ALWAYS:

IMG_libs = $(shell pkg-config --libs gdlib libjpeg libpng)
IMG_flags = $(shell pkg-config --cflags gdlib libjpeg libpng)
IMG_objs = resizer.o

% : %.o haar.o imgdb.o debug.o ${IMG_objs} # bloom_filter.o
	g++ -o $@ $^ ${CFLAGS} ${LDFLAGS} ${IMG_libs} ${DEFS} ${EXTRADEFS}

%.o : %.cpp
	g++ -c -o $@ $< -O2 ${CFLAGS} -DNDEBUG -Wall -DLinuxBuild -g ${IMG_flags} ${DEFS} ${EXTRADEFS}

%.S:	.ALWAYS
	g++ -S -o $@ $*.cpp -O2 ${CFLAGS} -DNDEBUG -Wall -DLinuxBuild -g ${IMG_flags} ${DEFS} ${EXTRADEFS}

