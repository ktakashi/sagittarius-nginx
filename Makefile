SAGITTARIUS_VERSION=0.9.4
NGINX_VERSION=1.15.5
VERSION=0.0.1

all: 
	$(MAKE) -C build NGINX_VERSION=$(NGINX_VERSION)

docker:
	docker build \
	-t sagittarius/nginx:$(VERSION) \
	--build-arg sagittarius_version=$(SAGITTARIUS_VERSION) \
	--build-arg host_nginx_version=$(NGINX_VERSION) \
	.

clean:
	$(MAKE) -C build NGINX_VERSION=$(NGINX_VERSION) clean
