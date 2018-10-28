SAGITTARIUS_CONFIG ?= sagittarius-config
NGINX_VERSION      ?= 1.15.5

INC_PATH=$(shell $(SAGITTARIUS_CONFIG) -I)

CC_OPT=-Wno-missing-field-initializers $(INC_PATH)
LD_OPT=$(shell $(SAGITTARIUS_CONFIG) -L)
MODULE_LOCATION=../../src

all: nginx

nginx: nginx-$(NGINX_VERSION)/objs/Makefile
	$(MAKE) -C nginx-$(NGINX_VERSION)

nginx-$(NGINX_VERSION)/objs/Makefile: nginx.tar.gz
	cd nginx-$(NGINX_VERSION) && \
	SAGITTARIUS_CONFIG=$(SAGITTARIUS_CONFIG) ./configure --with-compat \
	  --with-threads --with-select_module --with-poll_module \
	  --with-http_ssl_module --with-http_v2_module \
	  --with-http_realip_module \
	  --with-cc-opt="$(CC_OPT)" --with-ld-opt="$(LD_OPT)" \
	  --add-dynamic-module=$(MODULE_LOCATION)

nginx.tar.gz:
	curl -Lo nginx.tar.gz \
	  https://nginx.org/download/nginx-$(NGINX_VERSION).tar.gz && \
	tar xvf nginx.tar.gz

clean:
	rm -rf nginx-$(NGINX_VERSION) nginx.tar.gz

install:
	$(MAKE) -C nginx-$(NGINX_VERSION) install
