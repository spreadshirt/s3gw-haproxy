
#include "haproxy_redis.h"

#include <assert.h>

#include <types/global.h>

#include <types/fd.h>
#include <proto/fd.h>

#include <hiredis/async.h>

struct redisHaproxyAsync {
	int fd;
	struct redisAsyncContext *async;
};

static void ha_addRead(void *privdata) {
	struct redisHaproxyAsync *hap = privdata;
	fd_want_recv(hap->fd);
}

static void ha_delRead(void *privdata) {
	struct redisHaproxyAsync *hap = privdata;
	fd_cant_recv(hap->fd);
}

static void ha_addWrite(void *privdata) {
	struct redisHaproxyAsync *hap = privdata;
	fd_want_send(hap->fd);
}

static void ha_delWrite(void *privdata) {
	struct redisHaproxyAsync *hap = privdata;
	fd_cant_send(hap->fd);
}

static void ha_cleanup(void *privdata) {
	struct redisHaproxyAsync *hap = privdata;
	assert(hap);

	ha_delWrite(privdata);
	ha_delRead(privdata);

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
	struct redisHaproxyAsync *privdata;

	if (async->c.fd > global.maxsock)
		return REDIS_ERR_OTHER;

	privdata = (struct redisHaproxyAsync *) malloc(sizeof(*privdata));
	privdata->fd = async->c.fd;
	privdata->async = async;

	if (async->ev.data != NULL)
	    return REDIS_ERR;

	async->ev.addRead = ha_addRead;
	async->ev.addWrite = ha_addWrite;
	async->ev.delRead = ha_delRead;
	async->ev.delWrite = ha_delWrite;
	async->ev.cleanup = ha_cleanup;
	async->ev.data = privdata;

	fdtab[privdata->fd].owner = privdata;
	fdtab[privdata->fd].iocb = fd_handler;

	fd_insert(privdata->fd);

	return REDIS_OK;
}
