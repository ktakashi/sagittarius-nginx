NGINX_VERSION=1.15.5
MODULE_LOCATION=../../src

all: nginx

nginx: nginx-$(NGINX_VERSION)/objs/Makefile
	cd nginx-$(NGINX_VERSION) && make

nginx-$(NGINX_VERSION)/objs/Makefile: nginx.tar.gz
	cd nginx-$(NGINX_VERSION) && \
	  ./configure --add-dynamic-module=$(MODULE_LOCATION)

nginx.tar.gz:
	curl -Lo nginx.tar.gz \
	  https://nginx.org/download/nginx-$(NGINX_VERSION).tar.gz && \
	  tar xvf nginx.tar.gz

clean:
	rm -rf nginx-$(NGINX_VERSION) nginx.tar.gz
