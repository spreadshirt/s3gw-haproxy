#ifndef _PROTO_S3GW_H
#define _PROTO_S3GW_H

struct http_txn;

int s3gw_connect();
void s3gw_deinit();
void s3gw_enqueue(struct http_txn *txn);

extern int s3gw_enable;

#endif /* _PROTO_S3GW_H */
