
CFLAGS = -g -I../
LD_FLAGS = -L.. -lholeycow -lpthread -lrt

OBJS = benchmark.o

all: benchmark

benchmark: $(OBJS) ../libholeycow.a
	gcc $(OBJS) -o benchmark $(LD_FLAGS)

clean:
	rm -f *.o
