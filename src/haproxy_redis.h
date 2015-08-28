#ifndef HAPROXY_REDIS_H
#define HAPROXY_REDIS_H

struct redisAsyncContext;
int redisHaAttach(struct redisAsyncContext *async);

#endif /* HAPROXY_REDIS_H */
