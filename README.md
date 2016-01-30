# s3gw-haproxy

The s3gw-haproxy adds S3 bucket notifications for Ceph Rados GW. It basically works by
putting the HAProxy infront of the rgw.

See [README.orig](README.orig) for the original HAProxy README.

TODO: write a nice readme that explains all the details

## checkout
```
cd /usr/src
git clone https://github.com/spreadshirt/s3gw-haproxy
```

## build
```
cd s3gw-haproxy
dpkg-buildpackage -us -uc -b
```

## install
```
cd ..
dpkg -i haproxy*.deb
```

## configure

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
