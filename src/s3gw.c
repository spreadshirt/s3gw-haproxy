#include "s3gw.h"

#include <assert.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <types/proto_http.h>
#include <proto/proto_http.h>
#include <proto/log.h>

#include <types/global.h>

#include "haproxy_redis.h"

#define S3_LOG(proxy, level, format, ...) send_log(proxy, level, "S3: " format "\n", ## __VA_ARGS__)

static struct redisAsyncContext *ctx = NULL;

static void redis_msg_cb(struct redisAsyncContext *ctx, void *anon_reply, void *privdata) {
	redisReply *reply = anon_reply;
	if (reply->type == REDIS_REPLY_ERROR) {
		S3_LOG(NULL, LOG_ERR, "redis message failed: %s", reply->str);
	}
}

static void redis_disconnect_cb(const struct redisAsyncContext *ctx, int status) {
	if (status == REDIS_OK)
		S3_LOG(NULL, LOG_INFO, "disconnect from redis.");
	else
		S3_LOG(NULL, LOG_INFO, "disconnect from redis. becuase of an error.");
}

static void redis_connect_cb(const struct redisAsyncContext *ctx, int status) {
	if (status == REDIS_OK) {
		S3_LOG(NULL, LOG_INFO, "connected to redis.");
	} else {
		S3_LOG(NULL, LOG_ERR, "could not connect to redis!\n"
				      "redis errno %d"
				      "redis errstr: %s", ctx->err, ctx->errstr);
	}
}

void s3gw_connect() {
	if (global.s3.redis_ip && global.s3.redis_port) {
		ctx = redisAsyncConnect(global.s3.redis_ip, global.s3.redis_port);
	} else if (global.s3.unix_path) {
		ctx = redisAsyncConnectUnix(global.s3.unix_path);
	} else {
		// TODO: err message
	}

	if (!ctx) {
		// TODO: write a log message
		return;
	}
	redisHaAttach(ctx);
	redisAsyncSetConnectCallback(ctx, redis_connect_cb);
	redisAsyncSetDisconnectCallback(ctx, redis_disconnect_cb);
}

void s3gw_deinit() {
	if (ctx) {
		redisAsyncFree(ctx);
		ctx = NULL;
	}
}


/* TODO: add bucket prefix once started not for every message could safe a lot */
/* using %b makes it possible to use pointer + len like copy_source is */
/* WARNING: this must match the HTTP_METH */
const char *redisevent[HTTP_METH_OTHER] = {
	NULL, /* NONE */
	NULL, /* OPTIONS */
	NULL, /* GET */
	NULL, /* HEAD */
	"LPUSH %s:%b {\"event\":\"s3:ObjectCreated:Post\",\"objectKey\":\"%b\"}", /* POST */
	"LPUSH %s:%b {\"event\":\"s3:ObjectCreated:Put\",\"objectKey\":\"%b\"}", /* PUT */
	"LPUSH %s:%b {\"event\":\"s3:ObjectCreated:Delete\",\"objectKey\":\"%b\"}", /* DELETE */
};

const char *redis_copy_command = "LPUSH %s:%b {\"event\":\"s3:ObjectCreated:Copy\",\"objectKey\":\"%b\",\"src\":\"%b\"}";

/* split up the bucket and objectkey out of the uri */
int get_bucket_objectkey(
		struct http_txn *txn,
		const char **bucket,
		int *bucket_len,
		const char **objectkey,
		int *object_len) {

	/* e.g. uri = "POST /bar-fe80-eu/foobar HTTP 1.1"
	 * bucket is bar-fe80-eu
	 * key is foobar
	 */
	*bucket = NULL;
	*objectkey = NULL;

	const char *ptr;
	const char *start;
	const char *end;

	int count_slashs = 0;

	end = txn->req.chn->buf->p + txn->req.sl.rq.u + txn->req.sl.rq.u_l;
	start = ptr = http_get_path(txn);
	if (!ptr)
		return 1;

	while (ptr < end && *ptr != '?') {
		if (*ptr == '/')
			count_slashs++;
		else if (!*bucket && count_slashs == 1) {
			*bucket = ptr;
		} else if (!*objectkey && count_slashs == 2) {
			*bucket_len = ptr - start - 2; /* we are atm f of /foobar, so -2 would be the end */
			*objectkey = ptr;
		}
		ptr++;
	}

	if (count_slashs < 2 || *objectkey == NULL || *bucket == NULL)
		return 1;

	if(*ptr == '?')
		*object_len = ptr - *objectkey - 1;
	else if (ptr == end)
		*object_len = ptr - *objectkey;

	/* start points to the first "/"
	 * ptr points to last char before the query_string begins (/foobar/foo?go=foo, end will be o before '?')
	 * end to the end of the url
	 */

	return 0;
}

int check_bucket_valid_for_notification(const char *bucket, int bucket_len) {
	struct s3gw_buckets *buckets;

	list_for_each_entry(buckets, &global.s3.buckets, list) {
		if (!strncmp(buckets->bucket, bucket, bucket_len))
			return 1;
	}

	return 0;
}

/* enqueue the message */
void s3gw_enqueue(struct http_txn *txn) {
	int ret = 0;
	const char *bucket = "";
	int bucket_len = 0;
	const char *objectkey = "";
	int objectkey_len = 0;
	void *privdata = NULL;

	assert(txn);

	/* check if properly connected */
	if (!ctx || ctx->err) {
		/* only log at the moment */
		/* do reconnect and lock out the ctx to prevent other callbacks from doing the same */
		return;
	}

	if (txn->status < 200 || txn->status > 300) {
		return;
	}

	switch (txn->meth) {
		case HTTP_METH_DELETE:
		case HTTP_METH_POST:
			if (get_bucket_objectkey(txn, &bucket, &bucket_len, &objectkey, &objectkey_len)) {
				return;
			}
			if (!check_bucket_valid_for_notification(bucket, bucket_len)) {
				return;
			}
			ret = redisAsyncCommand(ctx, &redis_msg_cb, privdata, redisevent[txn->meth],
						global.s3.bucket_prefix,
						bucket, bucket_len,
						objectkey, objectkey_len);
			break;
		case HTTP_METH_PUT:
			if (get_bucket_objectkey(txn, &bucket, &bucket_len, &objectkey, &objectkey_len)) {
				return;
			}

			if (!check_bucket_valid_for_notification(bucket, bucket_len)) {
				return;
			}

			if (txn->s3gw.copy_source && txn->s3gw.copy_source_len > 0) {
				ret = redisAsyncCommand(ctx, &redis_msg_cb, privdata, redis_copy_command,
							global.s3.bucket_prefix,
							bucket, bucket_len,
							objectkey, objectkey_len,
							txn->s3gw.copy_source, txn->s3gw.copy_source_len);
			} else {
				ret = redisAsyncCommand(ctx, &redis_msg_cb, privdata, redisevent[txn->meth],
							global.s3.bucket_prefix,
							bucket, bucket_len,
							objectkey, objectkey_len);
			}
			break;
		default:
			return;
			break;
	}

	if (ret != REDIS_OK) {
		S3_LOG(NULL, LOG_ERR, "could not enque redis");
	}
}
