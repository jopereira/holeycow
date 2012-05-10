
HCOW_CFLAGS += -I $(HC_TOPDIR) -fPIC -O3
HCOW_LDFLAGS += -L $(HC_TOPDIR) -lholeycow -lpthread -laio

ifdef DISKSIM
HCOW_CFLAGS += -I $(DS_TOPDIR)/include -I $(DS_TOPDIR)/src -DDISKSIM
HCOW_LDFLAGS += -L $(DS_TOPDIR)/src/ -ldisksim -L $(DS_TOPDIR)/diskmodel/ -ldiskmodel \
	-L $(DS_TOPDIR)/libparam/ -lparam -L $(DS_TOPDIR)/libddbg/ -lddbg \
	-L $(DS_TOPDIR)/memsmodel/ -lmemsmodel -lm
endif

