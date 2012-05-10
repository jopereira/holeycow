
ifdef DISKSIM
DS_TOPDIR =  $(HC_TOPDIR)/../../disksim/disksim-4.0/
DS_CFLAGS = -I $(DS_TOPDIR)/include -I $(DS_TOPDIR)/src -DDISKSIM
DS_LDFLAGS = -L $(DS_TOPDIR)/src/ -ldisksim -L $(DS_TOPDIR)/diskmodel/ -ldiskmodel \
	-L $(DS_TOPDIR)/libparam/ -lparam -L $(DS_TOPDIR)/libddbg/ -lddbg \
	-L $(DS_TOPDIR)/memsmodel/ -lmemsmodel -lm
endif

CFLAGS = $(DS_CFLAGS) -I $(HC_TOPDIR) -fPIC -O3
LDFLAGS = -L $(HC_TOPDIR) -lholeycow -lpthread -laio $(DS_LDFLAGS)
