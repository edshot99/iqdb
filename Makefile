# Some configuration options
#----------------------------

# Any extra options you need
CFLAGS=-O2 -DNDEBUG -Wall -fomit-frame-pointer

# For GCC the C++14 support also needs to be enabled explicitly
override DEFS+=-std=c++14

# By default iqdb uses integer math for the similarity computation,
# because it is often slightly faster than floating point math
# (and iqdb cannot make use of SSE et.al.) You can remove this option
# if you wish to compare both versions. This setting has
# negligible impact on the value of the similarity result.
override DEFS+=-DINTMATH

# Needed by httplib
override DEFS+=-pthread

# -------------------------
#  no configuration below
# -------------------------

.SUFFIXES:

all:	iqdb

.PHONY: clean

clean:
	rm -f *.o vendor/*.o iqdb

%.o : %.h
%.o : %.cpp
iqdb.o : debug.h imgdb.h server.h
imgdb.o : imgdb.h imglib.h haar.h auto_clean.h delta_queue.h debug.h
server.o : auto_clean.h debug.h imgdb.h vendor/httplib.h vendor/json.hpp
test-db.o : imgdb.h delta_queue.h debug.h
haar.o :
debug.o :
resizer.o :
vendor/httplib.o : vendor/httplib.h
%.le.o : %.h
iqdb.le.o : imgdb.h haar.h auto_clean.h debug.h
imgdb.le.o : imgdb.h imglib.h haar.h auto_clean.h delta_queue.h debug.h
haar.le.o :

.ALWAYS:

IMG_libs = $(shell pkg-config --libs gdlib libjpeg libpng)
IMG_flags = $(shell pkg-config --cflags gdlib libjpeg libpng)
IMG_objs = resizer.o

% : %.o haar.o imgdb.o debug.o server.o vendor/httplib.o ${IMG_objs}
	g++ -o $@ $^ ${CFLAGS} ${LDFLAGS} ${IMG_libs} ${DEFS}

%.o : %.cpp
	g++ -c -o $@ $< ${CFLAGS} -DLinuxBuild ${IMG_flags} ${DEFS}

%.S:	.ALWAYS
	g++ -S -o $@ $*.cpp ${CFLAGS} -DLinuxBuild ${IMG_flags} ${DEFS}

