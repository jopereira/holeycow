
#include <sys/types.h>

/* Generic asynchronous block device */
struct device;

typedef void (*callback)(void*, int);

struct device_ops {
	void (*pwrite)(struct device*, void*, size_t, off_t, callback, void*);
	void (*pread)(struct device*, void*, size_t, off_t, callback, void*);
	int (*close)(struct device*);
};

struct device {
	struct device_ops* ops;
	void* data;
};

#define device_pwrite(dev, buf, size, off, cb, cookie) (dev)->ops->pwrite(dev, buf, size, off, cb, cookie)
#define device_pread(dev, buf, size, off, cb, cookie) (dev)->ops->pread(dev, buf, size, off, cb, cookie)
#define device_close(dev) (dev)->ops->close(dev)

/* Simulate synchronous I/O */
extern int device_pwrite_sync(struct device*, void*, size_t, off_t);
extern int device_pread_sync(struct device*, void*, size_t, off_t);

/* Enforce block aligned I/O */
extern void blockalign(struct device*, struct device*, int);
