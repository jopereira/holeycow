
#include <unistd.h>
#include <stdlib.h>

#include "posixbe.h"

#define PBE(dev) ((struct posixbe_data*)(dev)->data)

struct posixbe_data {
	int fd;
};

static void posixbe_pwrite(struct device* dev, void* buf, size_t size, off_t offset, callback cb, void* cookie) {
	int ret=pwrite(PBE(dev)->fd, buf, size, offset);
	cb(cookie, ret);
}

static void posixbe_pread(struct device* dev, void* buf, size_t size, off_t offset, callback cb, void* cookie) {
	int ret=pread(PBE(dev)->fd, buf, size, offset);
	cb(cookie, ret);
}

static int posixbe_close(struct device* dev) {
	int ret=close(PBE(dev)->fd);
	free(PBE(dev));
	return ret;
}

struct device_ops posixbe_device_ops = {
	posixbe_pwrite,
	posixbe_pread,
	posixbe_close
};

int posixbe_open(struct device* dev, char* path, int flags) {
	dev->ops = &posixbe_device_ops;
	dev->data = malloc(sizeof(struct posixbe_data));

	PBE(dev)->fd = open(path, flags, 0644);

	// TODO: error handling

	return 0;
}

