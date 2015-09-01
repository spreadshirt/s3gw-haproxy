#include <assert.h>

#include <types/fd.h>
#include <types/global.h>

#include <proto/fd.h>
#include <proto/haproxy_redis.h>

#include <hiredis/async.h>

struct redisHaproxyAsync {
	int fd;
	struct redisAsyncContext *async;
};

static void ha_addRead(void *privdata) {
	struct redisHaproxyAsync *hap = privdata;
	assert(hap);

	fd_want_recv(hap->fd);
}

static void ha_delRead(void *privdata) {
	struct redisHaproxyAsync *hap = privdata;
	assert(hap);

	fd_stop_send(hap->fd);
}

static void ha_addWrite(void *privdata) {
	struct redisHaproxyAsync *hap = privdata;
	assert(hap);

	fd_want_send(hap->fd);
}

static void ha_delWrite(void *privdata) {
	struct redisHaproxyAsync *hap = privdata;
	assert(hap);

	fd_stop_send(hap->fd);
}

static void ha_cleanup(void *privdata) {
	struct redisHaproxyAsync *hap = privdata;
	assert(hap);

	fd_stop_both(hap->fd);
	fd_delete(hap->fd);

	free(hap);
}

static int fd_handler(int fd) {
	struct redisHaproxyAsync *hap = fdtab[fd].owner;
	assert(hap);

	if (fdtab[fd].ev & FD_POLL_IN) {
		redisAsyncHandleRead(hap->async);
		fd_done_recv(fd);
	}

	if (fdtab[fd].ev & FD_POLL_OUT) {
		redisAsyncHandleWrite(hap->async);
	}

	return 0;
}

int redisHaAttach(redisAsyncContext *async) {
	struct redisHaproxyAsync *hap;

	if (async->c.fd > global.maxsock)
		return REDIS_ERR_OTHER;

	/* already attached */
	if (async->ev.data != NULL)
	    return REDIS_ERR;

	hap = (struct redisHaproxyAsync *) malloc(sizeof(*hap));
	hap->fd = async->c.fd;
	hap->async = async;

	async->ev.addRead = ha_addRead;
	async->ev.addWrite = ha_addWrite;
	async->ev.delRead = ha_delRead;
	async->ev.delWrite = ha_delWrite;
	async->ev.cleanup = ha_cleanup;
	async->ev.data = hap;

	fdtab[hap->fd].owner = hap;
	fdtab[hap->fd].iocb = fd_handler;

	fd_insert(hap->fd);

	return REDIS_OK;
}
