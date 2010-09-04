
CFLAGS = -I../../common
LD_FLAGS = -L../../common -lholeycow -lpthread

OBJS = main.o workload.o

all: benchmark

benchmark: $(OBJS)
	gcc $(OBJS) -o benchmark $(LD_FLAGS)

clean:
	rm -f *.o
