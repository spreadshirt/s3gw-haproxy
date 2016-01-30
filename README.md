# s3gw-haproxy

The s3gw-haproxy adds S3 bucket notifications for Ceph Rados GW. It basically works by
putting the HAProxy infront of the rgw.

See README.orig for the original HAProxy README.

TODO: write a nice readme that explains all the details

# checkout
cd /usr/src
git clone https://github.com/spreadshirt/s3gw-haproxy

# build
cd s3gw-haproxy
dpkg-buildpackage -us -uc -b

# install
cd ..
dpkg -i haproxy*.deb
