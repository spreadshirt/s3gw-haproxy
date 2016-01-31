# s3gw-haproxy

The s3gw-haproxy adds S3 bucket notifications for Ceph Rados GW. It basically works by
putting the HAProxy infront of the rgw.

See [README.orig](README.orig) for the original HAProxy README.

TODO: write a nice readme that explains all the details

## Checkout
```
cd /usr/src
git clone https://github.com/spreadshirt/s3gw-haproxy
```

## Build
```
cd s3gw-haproxy
dpkg-buildpackage -us -uc -b
```

## Install
```
cd ..
dpkg -i haproxy*.deb
```

## Configure

To enable bucket notifications, adjust the global section of your ```haproxy.cfg```. The notifications must be enabled in general and individual per bucket.

Example:
````
global
        s3.enable
        s3.redis_ip 127.0.0.1
        s3.redis_port 6379
        s3.bucket_prefix s3notifications
        s3.buckets mybucket1
        s3.buckets mybucket2
```

## Notifications

The notifications for PUT, POST and DELETE operations are published (LPUSH) to a redis queue with the name `<s3.bucket_prefix>:<bucket-name>` where `<bucket-name>` is the name of the actual bucket, e.g. a queue name could be like `s3notifications:mybucket`. The notification itself is a simple JSON with the fields event (what happened) and objectKey (to which object).

Example notification:
```
{
  "event": "s3:ObjectCreated:Put",
  "objectKey": "foobar"
}
```

| Field name | Description | Data Type | Possible Values |
| --- | --- | --- | --- |
| event | type | String (fixed set) | `s3:ObjectCreated:Put`<br>`s3:ObjectCreated:Post`<br>`s3:ObjectCreated:Copy`<br>`s3:ObjectRemoved:Delete` |
| objectKey | key of created or deleted object | String | (see S3 documentation for possible values) |
| source | only for PUT operations; value of `x-amz-copy-source header` (if set) (see [RESTObjectCopy](http://docs.aws.amazon.com/AmazonS3/latest/API/RESTObjectCOPY.html)) | String | `/<bucketName>/<objectKey>` |
