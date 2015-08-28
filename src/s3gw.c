#include "s3gw.h"

#include <assert.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <types/proto_http.h>
#include <proto/proto_http.h>

#include "haproxy_redis.h"

static struct redisAsyncContext *ctx = NULL;
static struct s3gw_config {
	const char **buckets;
	int buckets_len;
	const char *bucket_prefix;
	const char *ip;
	int port;
	const char *bind_ip;
	const char *path;
} config;

int s3gw_enable = 0;

void s3gw_configure(const char **buckets, int buckets_len, const char *bucket_prefix,
	      const char *ip, int port, const char *path) {
	config.buckets = buckets;
	config.buckets_len = buckets_len;
	config.bucket_prefix = bucket_prefix;
	config.ip = ip;
	config.port = port;
	config.path = path;
}

void s3gw_connect() {
	if (config.ip && config.port) {
		ctx = redisAsyncConnect(config.ip, config.port);
	} else if (config.path) {
		ctx = redisAsyncConnectUnix(config.path);
	} else {
		// TODO: err message
	}

	if (!ctx) {
		// TODO: write a log message
	}

	redisHaAttach(ctx);
}

void s3gw_deinit() {
	if (ctx) {
		redisAsyncFree(ctx);
		ctx = NULL;
	}
}

void redis_msg_cb(struct redisAsyncContext *ctx, void *replay, void *privdata) {
	/* debug output
	 * how can a header be used, as marker for debug output?
	 */
	/* debug it when it fails */
}

void redis_disconnect_cb(const struct redisAsyncContext *ctx, int status) {
	/* log output */
}

void redis_connect_cb(const struct redisAsyncContext *ctx, int status) {
	/* log output */
}

enum s3_event_t {
	S3_PUT = 0,
	S3_POST,
	S3_COPY,
	S3_DELETE,
};

/* TODO: add bucket prefix once started not for every message could safe a lot */
/* using %b makes it possible to use pointer + len like copy_source is */
const char *redisevent[] = {
	"LPUSH %s:%s {\"event\":\"s3:ObjectCreated:Put\",\"objectKey\":\"%s\"}",
	"LPUSH %s:%s {\"event\":\"s3:ObjectCreated:Post\",\"objectKey\":\"%s\"}",
	"LPUSH %s:%s {\"event\":\"s3:ObjectCreated:Copy\",\"objectKey\":\"%s\",\"src\":\"%b\"}",
	"LPUSH %s:%s {\"event\":\"s3:ObjectCreated:Delete\",\"objectKey\":\"%s\"}",
};

/* enqueue the message */
void s3gw_enqueue(struct http_txn *txn) {
	int ret = 0;
	const char *bucket = "";
	const char *objectkey = "";
	void *privdata = NULL;

	assert(txn);

	/* check if properly connected */
	if (!ctx || ctx->err) {
		/* only log at the moment */
		/* do reconnect and lock out the ctx to prevent other callbacks from doing the same */
		return;
	}

	switch (txn->meth) {
		case HTTP_METH_DELETE:
			ret = redisAsyncCommand(ctx, &redis_msg_cb, privdata, redisevent[S3_DELETE], bucket, objectkey);
			break;
		case HTTP_METH_POST:
			break;
		case HTTP_METH_PUT:
			if (txn->s3gw.copy_source && txn->s3gw.copy_source_len > 0) {
				ret = redisAsyncCommand(ctx, &redis_msg_cb, privdata, redisevent[S3_COPY],
							bucket, objectkey,
							txn->s3gw.copy_source, txn->s3gw.copy_source_len);
			} else {
				ret = redisAsyncCommand(ctx, &redis_msg_cb, privdata, redisevent[S3_PUT], bucket, objectkey);
			}
			break;
		default:
			return;
			break;
	}

	if (ret != REDIS_OK) {
		/* log output */
	}
}
