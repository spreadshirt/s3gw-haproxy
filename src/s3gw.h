#ifndef S3GW_H
#define S3GW_H

#include <common/mini-clist.h>

struct http_txn;

struct s3gw_buckets {
    struct list list;
    char *bucket;
};

int s3gw_connect();
void s3gw_deinit();
void s3gw_enqueue(struct http_txn *txn);

extern int s3gw_enable;

#endif /* S3GW_H */
