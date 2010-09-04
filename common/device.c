
#include "device.h"

#include <pthread.h>

struct cb_data {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int flag, ret;
};

static void sync_cb(void* cookie, int ret) {
	struct cb_data* data = (struct cb_data*) cookie;

	pthread_mutex_lock(&data->mutex);
	data->flag = 1;
	data->ret = ret;
	pthread_cond_notify(&data->cond);
	pthread_mutex_unlock(&data->mutex);
}

int device_pwrite_sync(struct device* dev, void* buf, size_t size, off_t offset) {
	struct cb_data data = {
		PTHREAD_MUTEX_INITIALIZER,
		PTHREAD_COND_INITIALIZER,
		0,
		0
	};

	device_pwrite(dev, buf, size, offset, sync_cb, &data);

	pthread_mutex_lock(&data.mutex);
	while(data.flag == 0)
		pthread_cond_wait(&data.cond, &data.mutex);
	pthread_mutex_unlock(&data.mutex);

	return data.ret;
}

int device_pread_sync(struct device* dev, void* buf, size_t size, off_t offset) {
	struct cb_data data = {
		PTHREAD_MUTEX_INITIALIZER,
		PTHREAD_COND_INITIALIZER,
		0,
		0
	};

	device_pread(dev, buf, size, offset, sync_cb, &data);

	pthread_mutex_lock(&data.mutex);
	while(data.flag == 0)
		pthread_cond_wait(&data.cond, &data.mutex);
	pthread_mutex_unlock(&data.mutex);

	return data.ret;
}

