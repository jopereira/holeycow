
OBJS = holeycow.o master_stab.o slave_stab.o device.o

all: libholeycow.a

libholeycow.a:$(OBJS)
	ar r libholeycow.a $(OBJS)

clean:
	rm -f *.o
