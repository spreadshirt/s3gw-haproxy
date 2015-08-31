#/usr/bin/env python3

import os
from io import StringIO
from threading import Thread
import socketserver
from http.server import SimpleHTTPRequestHandler
from tempfile import NamedTemporaryFile
from random import randint
from subprocess import Popen, check_call, check_output, CalledProcessError


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

    return Popen(['sh', '-c', 'redis-server --port %d' % port])

def start_http(port):
    httpd = socketserver.TCPServer(("", port), TestHttpHandler)
    return Thread(target=httpd.serve_forever)

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
        s3.buckets bar-fe80-eu

    defaults
        mode    http
        retries 3

    listen  fooapp 0.0.0.0:%d
    balance roundrobin
    server  app1_1 127.0.0.1:%d
    """ % (redis, haproxy, backend)
    
class TestHttpHandler(SimpleHTTPRequestHandler):
    valid_objects = {
                '/foo': (200, 'blabla'),
                '/bar': (200, 'blabla'),
                '/return_502': (502, 'error505'),
            }

    def generic_handle(self):
        overall_request_counter += 1
        if self.path in self.valid_objects:
            rc, content =  self.valid_objects[self.path]
            self.send_response(rc)
            self.end_headers()
            return content
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
            os.stat(os.path.dirname(__file__) + '../haproxy')
        except CalledProcessError:
            raise RuntimeError("no redis-server available")
        except FileNotFoundError:
            raise RuntimeError("You must compile haproxy before executing tests scripts")

        self.haproxy_port = get_free_port()
        self.redis_port = get_free_port()
        self.backend_port = get_free_port()

        self.haproxy_cfg = NamedTemporaryFile()
        self.haproxy_cfg.file.write(bytes(
            simple_redis_haproxy_cfg(
                redis=self.redis_port,
                haproxy=self.haproxy_port,
                backend=self.backend_port), 'utf-8'))

