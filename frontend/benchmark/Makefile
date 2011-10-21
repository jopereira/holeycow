
CFLAGS = -g -I../../common
LD_FLAGS = -L../../common -lholeycow -lpthread -lrt

OBJS = benchmark.o ../../backend/posix/posixbe.o ../../backend/aio/aiobe.o ../../backend/null/nullbe.o

all: benchmark

benchmark: $(OBJS) ../../common/libholeycow.a
	gcc $(OBJS) -o benchmark $(LD_FLAGS)

clean:
	rm -f *.o
