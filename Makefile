
CC			= gcc
LDFLAGS = -L. -lholey -lpthread 
CFLAGS 	= -g -fPIC
OBJS		= slave_stab.o master_stab.o holeycow.o mytime.o
LIBS		=	libholey.so

all: master slave

master: master_main.o workload.o $(LIBS)
	$(CC) -o $@ $< workload.o $(LDFLAGS)

slave: slave_main.o workload.o $(LIBS)
	$(CC) -o $@ $< workload.o $(LDFLAGS)

libholey.so: $(OBJS)
	$(CC) -shared $(OBJS) -o $@

test: master slave clean-tests
	mkdir data
	mkdir cow
	dd if=/dev/zero of=data/volume bs=16 count=1000
	LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):. ./master 1 data/volume &
	sleep 1
	LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):. ./slave 127.0.0.1 data/volume cow

clean-tests:
	rm -rf data cow

clean: clean-tests
	rm -f *.o master slave libholey.so *.cow *~

