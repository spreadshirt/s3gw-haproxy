#ifndef _TYPES_S3GW
#define _TYPES_S3GW

#include <common/mini-clist.h>

struct s3gw_buckets {
    struct list list;
    char *bucket;
};

#endif /* _TYPES_S3GW */
