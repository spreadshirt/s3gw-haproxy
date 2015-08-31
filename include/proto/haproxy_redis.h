#ifndef _PROTO_HAPROXY_REDIS_H
#define _PROTO_HAPROXY_REDIS_H

struct redisAsyncContext;
int redisHaAttach(struct redisAsyncContext *async);

#endif /* _PROTO_HAPROXY_REDIS_H */
