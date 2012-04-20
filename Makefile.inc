
TOPDIR = $(shell pwd)

CFLAGS = -I $(TOPDIR) -fPIC -O3 -g

%.o: %.c
	gcc -c $(CFLAGS) $< -o $@

