#include <assert.h>

#include <common/time.h>

#include <proto/haproxy_redis.h>
#include <proto/log.h>
#include <proto/proto_http.h>
#include <proto/s3gw.h>
#include <proto/task.h>

#include <types/global.h>
#include <types/proto_http.h>
#include <types/s3gw.h>
#include <types/task.h>

#include <hiredis/hiredis.h>

#define S3_LOG(proxy, level, format, ...) send_log(proxy, level, "[s3] " format "\n", ## __VA_ARGS__)

static struct redisContext *ctx = NULL;
static struct task *reconnect_task = NULL;
static int redis_is_connected = 0;

/* called by redis_reconnect task */
static struct task *redis_reconnect(struct task *t) {
	redis_is_connected = 0;
	if (ctx) {
		redisFree(ctx);
		ctx = NULL;
	}

	if (s3gw_connect(0)) {
		S3_LOG(NULL, LOG_ERR, "reconnect to Redis failed. Retry in 1 second.");
		t->expire = tick_add(now_ms, 10000);
		return t;
	} else {
		t->expire = 0;
	}

	return t;
}

static void schedule_redis_reconnect() {
	if (reconnect_task == NULL) {
		reconnect_task = task_new();
		if (!reconnect_task) {
			/* no memory */
			return;
		}

		reconnect_task->process = redis_reconnect;
	} else if (tick_isset(reconnect_task->expire)) { /* check if alrady enqueued */
			return;
	}

	task_schedule(reconnect_task, tick_add(now_ms, 1000)); /* try again in 1 second */
}

/* return 0 if everything ok or wrong configured.
 * retcode is used to define if a reconnect is required. */
int s3gw_connect(int initial) {
	if (LIST_ISEMPTY(&global.s3.buckets)) {
		send_log(NULL, LOG_ERR, "s3 notifications enabled but no buckets are defined. Disabling s3 notifications.");
		global.s3.enabled = 0;
		return 0;
	}

	if (global.s3.redis_ip && global.s3.redis_port) {
		ctx = redisConnect(global.s3.redis_ip, global.s3.redis_port);
	} else if (global.s3.redis_unix_path) {
		ctx = redisConnectUnix(global.s3.redis_unix_path);
	} else {
		send_log(NULL, LOG_ERR, "s3 notifications enabled but no Redis server is configured.\n");
		send_log(NULL, LOG_ERR, "configure a Redis server or a unix path\n");
		send_log(NULL, LOG_ERR, "Disabling s3 notifications.\n");
		global.s3.enabled = 0;
		return 0;
	}

	if (!ctx || ctx->err) {
		if (initial)
			schedule_redis_reconnect();
		return 1;
	}

	redis_is_connected = 1;

	return 0;
}

void s3gw_deinit() {
	global.s3.enabled = 0;

	if (ctx) {
		redisFree(ctx);
		ctx = NULL;
	}

	if (reconnect_task) {
		task_free(reconnect_task);
		reconnect_task = NULL;
	}
}

/* TODO: add bucket prefix once started not for every message could safe a lot */
/* using %b makes it possible to use pointer + len like copy_source is */
/* WARNING: this must match the HTTP_METH */
static const char *redisevent[HTTP_METH_OTHER] = {
	NULL, /* NONE */
	NULL, /* OPTIONS */
	NULL, /* GET */
	NULL, /* HEAD */
	"LPUSH %s:%b {\"event\":\"s3:ObjectCreated:Post\",\"objectKey\":\"%b\"}", /* POST */
	"LPUSH %s:%b {\"event\":\"s3:ObjectCreated:Put\",\"objectKey\":\"%b\"}", /* PUT */
	"LPUSH %s:%b {\"event\":\"s3:ObjectRemoved:Delete\",\"objectKey\":\"%b\"}", /* DELETE */
};

static const char *redis_copy_command = "LPUSH %s:%b {\"event\":\"s3:ObjectCreated:Copy\",\"objectKey\":\"%b\",\"source\":\"%s\"}";

/* split up the bucket and objectkey out of the uri */
static int get_bucket_objectkey(
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

	S3_LOG(NULL, LOG_INFO, "txn->uri: %s", txn->uri);

	if (!txn->s3gw.path)
		return 1;

	start = ptr = txn->s3gw.path;
	end = ptr + strlen(txn->s3gw.path);

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
		*object_len = ptr - *objectkey;
	else if (ptr == end)
		*object_len = ptr - *objectkey;

	/* start points to the first "/"
	 * ptr points to last char before the query_string begins (/foobar/foo?go=foo, end will be o before '?')
	 * end to the end of the url
	 */

	return 0;
}

static int check_bucket_valid_for_notification(const char *bucket, int bucket_len) {
	struct s3gw_buckets *buckets;

	list_for_each_entry(buckets, &global.s3.buckets, list) {
		if (!strncmp(buckets->bucket, bucket, bucket_len))
			return 1;
	}

	return 0;
}

static void check_redis_state(redisReply *reply) {
	if (!reply) {
		return;
	}
	if (reply->type == REDIS_REPLY_ERROR) {
		S3_LOG(NULL, LOG_ERR, "Redis message failed: %s", reply->str);
		schedule_redis_reconnect();
	}
}

/* enqueue the message */
void s3gw_enqueue(struct http_txn *txn) {
	int ret = 0;
	redisReply *reply = NULL;
	const char *bucket = "";
	int bucket_len = 0;
	const char *objectkey = "";
	int objectkey_len = 0;

	assert(txn);

	/* check if properly connected */
	if (!redis_is_connected || !ctx || ctx->err) {
		/* only log at the moment */
		return;
	}

	if (txn->status < 200 || txn->status > 300) {
		return;
	}

	switch (txn->meth) {
		case HTTP_METH_DELETE:
			if (strstr(txn->uri, "?uploadId=") != NULL) {
                S3_LOG(NULL, LOG_INFO, "skip notification for multipart abort");
                return;
            }
		case HTTP_METH_POST:
			if (get_bucket_objectkey(txn, &bucket, &bucket_len, &objectkey, &objectkey_len)) {
				return;
			}
			S3_LOG(NULL, LOG_INFO, "object key: '%s', len: %d", objectkey, objectkey_len);

			if (!check_bucket_valid_for_notification(bucket, bucket_len)) {
				S3_LOG(NULL, LOG_INFO, "bucket '%s' not enabled for notifications", bucket);
				return;
			}

			S3_LOG(NULL, LOG_INFO, "publish notification");

			reply = redisCommand(ctx, redisevent[txn->meth],
						global.s3.bucket_prefix,
						bucket, bucket_len,
						objectkey, objectkey_len);
			check_redis_state(reply);
			break;
		case HTTP_METH_PUT:
			if (get_bucket_objectkey(txn, &bucket, &bucket_len, &objectkey, &objectkey_len)) {
				return;
			}
			S3_LOG(NULL, LOG_INFO, "object key: '%s', len: %d", objectkey, objectkey_len);

			if (!check_bucket_valid_for_notification(bucket, bucket_len)) {
				S3_LOG(NULL, LOG_INFO, "bucket '%s' not enabled for notifications", bucket);
				return;
			}

			if (txn->s3gw.copy_source) {
				S3_LOG(NULL, LOG_INFO, "publish notification (with copy source)");

				reply = redisCommand(ctx, redis_copy_command,
							global.s3.bucket_prefix,
							bucket, bucket_len,
							objectkey, objectkey_len,
							txn->s3gw.copy_source);
				check_redis_state(reply);

			} else {
				S3_LOG(NULL, LOG_INFO, "publish notification");

				reply = redisCommand(ctx, redisevent[txn->meth],
							global.s3.bucket_prefix,
							bucket, bucket_len,
							objectkey, objectkey_len);
				check_redis_state(reply);
			}
			break;
		default:
			S3_LOG(NULL, LOG_INFO, "ignore HTTP method %d", txn->meth);
			return;
			break;
	}

	if (ret != REDIS_OK) {
		S3_LOG(NULL, LOG_ERR, "could not enqueue notification");
	}
}
