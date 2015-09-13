#ifndef _PROTO_S3GW_H
#define _PROTO_S3GW_H

struct http_txn;

/* inital = 1 if called form main haproxy */
int s3gw_connect(int initial);
void s3gw_deinit();
void s3gw_enqueue(struct http_txn *txn);

extern int s3gw_enable;

#endif /* _PROTO_S3GW_H */
