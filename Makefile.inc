
TOPDIR = $(shell pwd)

CFLAGS = -I $(TOPDIR) -fPIC -O3

%.o: %.c
	gcc -c $(CFLAGS) $< -o $@

