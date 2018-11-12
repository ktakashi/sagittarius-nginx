SAGITTARIUS_CONFIG ?= sagittarius-config
NGINX_VERSION      ?= 1.15.5

INC_PATH=$(shell $(SAGITTARIUS_CONFIG) -I)

OPTS=-Wno-missing-field-initializers $(INC_PATH)
MODULE_LOCATION=../../src
SANDBOX=nginx-$(NGINX_VERSION)/sandbox

ifeq ($(shell uname), Darwin)
  CC_OPT=$(OPTS) -I/usr/local/opt/openssl/include
  WITH_LD_OPT=--with-ld-opt="$(LDFLAGS) -L/usr/local/opt/openssl/lib"
else
  CC_OPT=$(OPTS)
endif


all: nginx

nginx: nginx-$(NGINX_VERSION)/objs/Makefile
	$(MAKE) -C nginx-$(NGINX_VERSION)

nginx-$(NGINX_VERSION)/objs/Makefile: nginx.tar.gz
	cd nginx-$(NGINX_VERSION) && \
	SAGITTARIUS_CONFIG=$(SAGITTARIUS_CONFIG) ./configure --with-compat \
	  --with-threads --with-select_module --with-poll_module \
	  --with-http_ssl_module --with-http_v2_module \
	  --with-http_realip_module \
	  --with-cc-opt="$(CC_OPT)" $(WITH_LD_OPT)\
	  --add-dynamic-module=$(MODULE_LOCATION)

nginx.tar.gz:
	curl -Lo nginx.tar.gz \
	  https://nginx.org/download/nginx-$(NGINX_VERSION).tar.gz && \
	tar xvf nginx.tar.gz

clean:
	rm -rf nginx-$(NGINX_VERSION) nginx.tar.gz

install:
	$(MAKE) -C nginx-$(NGINX_VERSION) install

run: prep example
	./nginx-$(NGINX_VERSION)/objs/nginx \
	  -c $(shell pwd)/../conf/example.conf \
	  -p $(shell pwd)/$(SANDBOX)

example:
	mkdir -p $(SANDBOX)/lib $(SANDBOX)/example
	cp -r ../example/* $(SANDBOX)/example

prep: nginx
	mkdir -p $(SANDBOX)/logs $(SANDBOX)/modules
	cp nginx-$(NGINX_VERSION)/objs/ngx_http_sagittarius_module.so \
	   $(SANDBOX)/modules/
	cp -r ../scheme/sagittarius $(SANDBOX)/lib

stop:
	./nginx-$(NGINX_VERSION)/objs/nginx -s stop \
	-c $(shell pwd)/../conf/example.conf \
	-p $(shell pwd)/$(SANDBOX)

check: prep test-lib
	./nginx-$(NGINX_VERSION)/objs/nginx \
	  -c $(shell pwd)/../test/conf/test.conf \
	  -p $(shell pwd)/$(SANDBOX)

test-lib:
	mkdir -p $(SANDBOX)/lib $(SANDBOX)/test/web
	cp -r ../test/web/* $(SANDBOX)/test/web

check-stop:
	./nginx-$(NGINX_VERSION)/objs/nginx -s stop \
	-c $(shell pwd)/../test/conf/test.conf \
	-p $(shell pwd)/$(SANDBOX)
