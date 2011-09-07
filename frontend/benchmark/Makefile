
CFLAGS = -g -I../../common
LD_FLAGS = -L../../common -lholeycow -lpthread -lrt

OBJS = main.o workload.o ../../backend/posix/posixbe.o ../../backend/aio/aiobe.o

all: benchmark

benchmark: $(OBJS) ../../common/libholeycow.a
	gcc $(OBJS) -o benchmark $(LD_FLAGS)

clean:
	rm -f *.o
