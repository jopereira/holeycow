
HC_TOPDIR = $(CURDIR)
DS_TOPDIR =  $(HC_TOPDIR)/../../disksim/disksim-4.0/

include Makefile.inc

CFLAGS = $(HCOW_CFLAGS) -fPIC -O3
LDFLAGS = $(HCOW_LDFLAGS)

ifdef DISKSIM
DS_OBJS = backend/simbe.o
endif

OBJS = src/device.o \
	src/nullbe.o src/aiobe.o src/posixbe.o $(DS_OBJS)

all: libholeycow.a benchmark/benchmark

libholeycow.a: $(OBJS)
	ar rs libholeycow.a $(OBJS)

benchmark/benchmark: benchmark/benchmark.o libholeycow.a
	cc benchmark/benchmark.o -o $@ $(CFLAGS) $(LDFLAGS)

clean:
	rm -f $(OBJS) benchmark/benchmark benchmark/benchmark.o libholeycow.a

