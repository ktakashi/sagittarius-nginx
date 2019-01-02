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

Directives
----------

- `sagittarius` *entry* *[init]* *[cleanup]* - **required**

This directive must be inside of a location directive. It is illegal to 
put more than one `sagittarius` directive in one location directive.

The directive is a block directive, and it must have a `library` directive
inside. 

The specified location will be an entry point of the *entry* procedure which
must be defined in the specified library. (The library name must be specified
by the `library` directive.) The *entry* procedure is called with 2 arguments,
NGINX request and NGINX response, and must return 2 values, status and content
type.

If the optional *init* and *cleanup* are specified, then they will be called
on the very first time of the HTTP request and when the worker process is
terminating, respectively. 

The *init* and *cleanup* procedures are called with one argument, NGINX context.

NOTE: The *cleanup* is only called if the *init* is called even if the worker
process is terminating.

- `library` *name* - **required**

The library which provides *entry* described in the `sagittarius` directive.

- `load_path` *path1* *[paths ...]* - **optional**

Specifiying load path. This directive can occure multiple times.

- `parameter` *name* *value* - **optional**

Adding a context parameter named *name* with value of *value*. This is useful
if users want to share the same values per context.

Glossaries:

- *context*: An application context. A context contains the same information
  among the worker processes. This is equivalent with the location path.

Scheme APIs
===========

The Scheme APIs provides accessor of the HTTP request, response and NGIN
context.

HTTP request
------------

All the HTTP request access procedures are prefixed with `nginx-request-`.

- `(nginx-request? obj)`:

  Returns `#t` if the given *obj is a HTTP request otherwiese `#f`.

- `(nginx-request-input-port request)`:

  Returns binary input port of transfered data. This procedure **always**
  returns an binary input port. If it's a GET request, reading a data 
  always returns EOF.

- `(nginx-request-context request)`:

  Returns NGINX context of this HTTP request.

- `(nginx-request-method request)`:

  Retunrs HTTP method of this request as a string value.

- `(nginx-request-uri request)`:

  Returns URI of this request.

- `(nginx-request-headers request)`:

  Returns alist of HTTP header name and its value.

- `(nginx-request-cookies request)`:

  Returns a list of HTTP cookies of `(rfc cookies)`.

- `(nginx-request-query-string request)`:

  Returns a query string of the HTTP request.

- `(nginx-request-original-uri request)`:

  Returns the original URI of the HTTP request. The returning value
  may contain query string and fragment.

- `(nginx-request-request-line request)`:

  Returns the first line of the HTTP reuqest.

The followings are the convenient procedures to access HTTP headers.
The procedure name itself should be descriptive enough to see which
HTTP headers are returned by the procedures.

- `(nginx-request-host request)`:
- `(nginx-request-connection request)`:
- `(nginx-request-if-modified-since request)`:
- `(nginx-request-if-unmodified-since request)`:
- `(nginx-request-if-match request)`:
- `(nginx-request-if-none-match request)`:
- `(nginx-request-user-agent request)`:
- `(nginx-request-referer request)`:
- `(nginx-request-content-length request)`:
- `(nginx-request-content-range request)`:
- `(nginx-request-content-type request)`:
- `(nginx-request-range request)`:
- `(nginx-request-if-range request)`:
- `(nginx-request-transfer-encoding request)`:
- `(nginx-request-te request)`:
- `(nginx-request-expect request)`:
- `(nginx-request-upgrade request)`:
- `(nginx-request-accept-encoding request)`:
- `(nginx-request-via request)`:
- `(nginx-request-authorization request)`:
- `(nginx-request-keep-alive request)`:
- `(nginx-request-accept request)`:
- `(nginx-request-accept-language request)`:


HTTP response
-------------

- `(nginx-response? obj)`:

  Returns `#t` if the given *obj is a HTTP response otherwiese `#f`.

- `(nginx-response-output-port response)`:

  Returns binary output port of this HTTP response. This output port
  represents the response content. Thus, writing to this port means
  returning content to the client.

- `(nginx-response-headers response)`:

  Reuturns alist of HTTP headers of the response.
  The result may or may not be freshly created.

- `(nginx-response-header-add! response name value)`:

  Adding an HTTP header of *name* with value *value*.
  
  This procedure appends the header.

- `(nginx-response-header-set! response name value)`:

  Setting an HTTP header of *name* with value *value*.
  
  This procedure replaces existing header(s).
  
- `(nginx-response-header-remove! response name)`:

  Removes an HTTP header of *name* if exists.

NGINX context
-------------

- `(nginx-context? context)`:

 Returns `#t` if the given *obj* is a NGINX context otherwiese `#f`.

- `(nginx-context-path context)`

  Returns the application context path of the given *context*.
  
  The value shall be the same as the path specified in `location` directive.
  
- `(nginx-context-parameter-ref context name)`

  Returns the parameter value associated to *name* from the *context*.
  If the parameter doesn't exist, then returns `#f`.

  The parameter value is always a string.

- `(nginx-context-parameters context)`:

  Returns an immutable hash table which contains all parameters of the 
  *context*.
