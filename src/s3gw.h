#ifndef S3GW_H
#define S3GW_H

struct http_txn;

struct s3gw_buckets {
    struct list list;
    char *bucket;
};

void s3gw_configure(const char **buckets, int buckets_len, const char *bucket_prefix,
	      const char *ip, int port, const char *path);
void s3gw_connect();
void s3gw_deinit();
void s3gw_enqueue(struct http_txn *txn);

extern int s3gw_enable;

#endif /* S3GW_H */
