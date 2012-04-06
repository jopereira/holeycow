
CFLAGS = -g -I../
LD_FLAGS = -L.. -lholeycow -lpthread -laio

OBJS = benchmark.o

all: benchmark

benchmark: $(OBJS) ../libholeycow.a
	gcc $(OBJS) -o benchmark $(LD_FLAGS)

clean:
	rm -f *.o
