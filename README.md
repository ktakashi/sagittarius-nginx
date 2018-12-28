Sagittarius module for NGINX
============================

This repository provides Sagittarius module for NGINX. Using this,
users can write web application in Scheme easily.

Requirements
============

This module requires Sagittarius **0.9.5**. The later version may work
as long as the undocumented C APIs aren't changed.

How to build
============

The module can only be built on POSIX environment (e.g. Linux, macOS).

It provides `Makefile` to detect and build automatically so simply run
the following command:

```shell
$ make
```

The module will be built in the `./build/nginx-*$version*/objs/` directory
with the name of `ngx_http_sagittarius_module.so`. The *$version* is currently
**1.15.8**.

If you want to run example application, then execute the `run` target.
The `run` target also requires `docker`. If you don't have it, just modify
it.

NGINX config
============

The module is built as a dynamic module, so add the following line:

```
load_module modules/ngx_http_sagittarius_module.so;
```

The module extends the `location` directive. The following example
adds a simple web application:

```
location /echo {
  sagittarius run init clean {
    load_path /opt/share/;
	library "(echo)";
  }
}
```
The `sagittarius` directive specifies that the location is handled by
the module. After the directive, it accepts 1 to 3 arguments; entry point,
context initialisation, and clean up. The first one is mandatory and the
rest are optional. All arguments must be exported procedures from the
library specified `library` directive, if specified.

Scheme APIs
===========

TBD
