#/usr/bin/env python3

import os
from io import StringIO
from threading import Thread
from time import sleep
import socketserver
import requests
import redis
from http.server import SimpleHTTPRequestHandler
from tempfile import NamedTemporaryFile
from random import randint
from subprocess import Popen, check_call, check_output, CalledProcessError
from nose.tools import eq_

overall_request_counter = 0

def check_port_is_free(port):
    try:
        check_call('netstat -ltnu | grep :%d ' % port, shell=True)
    except CalledProcessError as cpe:
        if cpe.returncode == 1:
            return True
        else:
            raise
    return False

def get_free_port(lower=33*1024, upper=63*1024):
    """ look for a free port """
    maxtries = 10
    for m in range(maxtries):
        port = randint(lower, upper)
        if check_port_is_free(port):
            return port
    raise RuntimeError("Can not get a free port")

def start_redis(port=None):
    if port == None:
        port = get_free_port()
    return Popen(['sh', '-c', 'redis-server --port %d --loglevel warning' % port])

def stop_redis(redis):
    redis.terminate()

def start_haproxy(config_file):
    return Popen(['sh', '-c', './haproxy -f %s' % config_file])

def stop_haproxy(haproxy):
    haproxy.terminate()

def start_http(port):
    httpd = socketserver.TCPServer(("", port), TestHttpHandler)
    t = Thread(target=httpd.serve_forever)
    t.daemon = False
    t.start()
    return (t, httpd)

def stop_http(httpdobj):
    t, httpd = httpdobj
    httpd.shutdown()
    t.join(1)

def simple_redis_haproxy_cfg(redis=None, haproxy=None, backend=None):
    """ generate a simple haproxy configuration with redis port %redis and haproxy port %haproxy """
    if not redis or not haproxy or not backend:
        raise RuntimeError("Missing argument.")

    return """ # haproxy test configuration
global
	s3.enable
	s3.redis_ip 127.0.0.1
	s3.redis_port %d
	s3.bucket_prefix bucket
	s3.buckets foo
	s3.buckets test-bucket

defaults
	mode    http
	retries 3
	
listen  fooapp 0.0.0.0:%d
	balance roundrobin
	server  app1_1 127.0.0.1:%d
    """ % (redis, haproxy, backend)
    
class TestHttpHandler(SimpleHTTPRequestHandler):
    valid_objects = {
                '/test-bucket/foo-key': (200, 'blabla'),
                '/test-bucket/bar-key': (200, 'blabla'),
                '/test-bucket/notfoundkey': (404, '404'),
            }

    def generic_handle(self):
        global overall_request_counter
        overall_request_counter += 1
        if self.path in self.valid_objects:
            rc, content =  self.valid_objects[self.path]
            self.send_response(rc)
            self.end_headers()
            self.wfile.write(bytes(content, 'utf-8'))
        else:
            self.send_response(404)
            self.end_headers()
            return

    def do_GET(self):
        return self.generic_handle()

    def do_HEAD(self):
        return self.generic_handle()

    def do_POST(self):
        return self.generic_handle()

    def do_PUT(self):
        return self.generic_handle()

    def do_DELETE(self):
        return self.generic_handle()

class TestRedis(object):
    def __init__(self):
        # check first if all required applications are available
        try:
            check_output('type redis-server', shell=True)
            os.stat('./haproxy')
        except CalledProcessError:
            raise RuntimeError("no redis-server available")
        except FileNotFoundError:
            raise RuntimeError("You must compile haproxy before executing tests scripts and execute nosetest from haproxy top dir")

        self.haproxy_port = get_free_port()
        self.redis_port = get_free_port()
        self.backend_port = get_free_port()

        self.haproxy_cfg = NamedTemporaryFile()
        self.haproxy_cfg.file.write(bytes(
            simple_redis_haproxy_cfg(
                redis=self.redis_port,
                haproxy=self.haproxy_port,
                backend=self.backend_port), 'utf-8'))
        self.haproxy_cfg.file.flush()
        self.redis = None
        self.http = None
        self.haproxy = None

    def setup(self):
        self.redis = start_redis(self.redis_port)
        self.http = start_http(self.backend_port)
        self.haproxy = start_haproxy(self.haproxy_cfg.name)
        sleep(1)

    def test_redis_reconnect(self):
        reqs = requests.Session()
        for n in range(32):
            reqs.post("http://127.0.0.1:%d/test-bucket/foo-key" % self.haproxy_port, data="foo")
        rs = redis.StrictRedis(host='localhost', port=self.redis_port)
        eq_(rs.llen("bucket:test-bucket"), 32)
        del rs
        stop_redis(self.redis)
        reqs.post("http://127.0.0.1:%d/test-bucket/foo-key" % self.haproxy_port, data="foo")
        sleep(1)
        self.redis = start_redis(self.redis_port)
        sleep(2)
        for n in range(4):
            reqs.post("http://127.0.0.1:%d/test-bucket/foo-key" % self.haproxy_port, data="foo")
        rs = redis.StrictRedis(host='localhost', port=self.redis_port)
        eq_(rs.llen("bucket:test-bucket"), 4)
        print("wokring test")

if __name__ == '__main__':
    # or run by nostests test_s3/
    r = TestRedis()
    r.setup()
    r.test_redis_reconnect()
