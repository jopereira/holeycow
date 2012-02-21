
include Makefile.inc

OBJS = common/holeycow.o common/master_stab.o common/slave_stab.o common/device.o \
	backend/null/nullbe.o backend/aio/aiobe.o backend/posix/posixbe.o

all: libholeycow.a

libholeycow.a: $(OBJS)
	ar r libholeycow.a $(OBJS)

clean:
	rm -f $(OBJS)
