XEN_ROOT = ../../..
include $(XEN_ROOT)/tools/Rules.mk

IBIN         = blktapctrl tapdisk
QCOW_UTIL    = img2qcow qcow2raw qcow-create
LIBAIO_DIR   = ../../libaio/src

CFLAGS   += -Werror
CFLAGS   += -Wno-unused
CFLAGS   += -I../lib
CFLAGS   += $(CFLAGS_libxenctrl)
CFLAGS   += $(CFLAGS_libxenstore)
CFLAGS   += -I $(LIBAIO_DIR)
CFLAGS   += -D_GNU_SOURCE
#TODO chnaged here
CFLAGS   += `pkg-config --libs --cflags glib-2.0`
CFLAGS   += -lpthread
CFLAGS   += `pkg-config openssl --libs`

# Get gcc to generate the dependencies for us.
CFLAGS   += -Wp,-MD,.$(@F).d
DEPS      = .*.d

ifeq ($(shell . ./check_gcrypt),"yes")
CFLAGS += -DUSE_GCRYPT
CRYPT_LIB := -lgcrypt
else
CRYPT_LIB := -lcrypto
$(warning *** libgcrypt not installed: falling back to libcrypto ***)
endif

LDFLAGS_blktapctrl := $(LDFLAGS_libxenctrl) $(LDFLAGS_libxenstore) -L../lib -lblktap
LDFLAGS_img := $(LIBAIO_DIR)/libaio.a $(CRYPT_LIB) -lpthread -lz

BLK-OBJS-y  := block-aio.o
#TODO changed here
BLK-OBJS-y  += block-holey.o
BLK-OBJS-y  += block-els.o
BLK-OBJS-y  += block-sync.o
BLK-OBJS-y  += block-vmdk.o
BLK-OBJS-y  += block-ram.o
BLK-OBJS-y  += block-qcow.o
BLK-OBJS-y  += block-qcow2.o
BLK-OBJS-y  += aes.o
BLK-OBJS-y  += tapaio.o
BLK-OBJS-y  += holeyaio.o
BLK-OBJS-$(CONFIG_Linux) += blk_linux.o

BLKTAB-OBJS-y := blktapctrl.o
BLKTAB-OBJS-$(CONFIG_Linux) += blktapctrl_linux.o

all: $(IBIN) qcow-util

blktapctrl: $(BLKTAB-OBJS-y)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDFLAGS_blktapctrl)

tapdisk: tapdisk.o $(BLK-OBJS-y)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDFLAGS_img)

.PHONY: qcow-util
qcow-util: img2qcow qcow2raw qcow-create

img2qcow qcow2raw qcow-create: %: %.o $(BLK-OBJS-y)
	$(CC) $(CFLAGS) -o $* $^ $(LDFLAGS) $(LDFLAGS_img)

install: all
	$(INSTALL_PROG) $(IBIN) $(QCOW_UTIL) $(VHD_UTIL) $(DESTDIR)$(SBINDIR)

clean:
	rm -rf *.o *~ $(DEPS) xen TAGS $(IBIN) $(LIB) $(QCOW_UTIL) $(VHD_UTIL)

.PHONY: clean install

-include $(DEPS)
