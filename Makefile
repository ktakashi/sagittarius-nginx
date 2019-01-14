# command
SAGITTARIUS_CONFIG ?= sagittarius-config
RM        ?= rm -f
INSTALL   ?= install
MKDIR     ?= $(INSTALL) -d

# This works only on 0.9.5...
SAGITTARIUS_PREFIX=$(shell $(SAGITTARIUS_CONFIG) --install-prefix)
SAGITTARIUS_PKGDIR=$(shell $(SAGITTARIUS_CONFIG) --pkglibdir)
SAGITTARIUS_SITELIB=$(SAGITTARIUS_PREFIX)/$(SAGITTARIUS_PKGDIR)

# docker info
SAGITTARIUS_VERSION=0.9.5
NGINX_VERSION=1.15.8
VERSION=1.0.0

all: 
	$(MAKE) -C build NGINX_VERSION=$(NGINX_VERSION)\
	  SAGITTARIUS_CONFIG=$(SAGITTARIUS_CONFIG)

docker:
	docker build \
	-t sagittariusscheme/nginx:$(VERSION) \
	-t sagittariusscheme/nginx:latest \
	--build-arg sagittarius_version=$(SAGITTARIUS_VERSION) \
	.

push:
	docker push sagittariusscheme/nginx:$(VERSION)
	docker push sagittariusscheme/nginx:latest
clean:
	$(MAKE) -C build NGINX_VERSION=$(NGINX_VERSION) \
	  SAGITTARIUS_CONFIG=$(SAGITTARIUS_CONFIG) clean

install:
	$(MKDIR) $(DESTDIR)$(SAGITTARIUS_SITELIB)/sagittarius
	$(INSTALL) -m0644 scheme/sagittarius/*.scm $(DESTDIR)$(SAGITTARIUS_SITELIB)/sagittarius

install-nginx:
	$(MAKE) -C build NGINX_VERSION=$(NGINX_VERSION) \
	  SAGITTARIUS_CONFIG=$(SAGITTARIUS_CONFIG) install

# nginx doesn't provide uninstall target. sad...
uninstall:
	$(RM) $(DESTDIR)$(SAGITTARIUS_SITELIB)/sagittarius/nginx.scm

run:
	./scripts/redis.sh start
	$(MAKE) -C build run NGINX_VERSION=$(NGINX_VERSION)\
	  SAGITTARIUS_CONFIG=$(SAGITTARIUS_CONFIG)

stop:
	$(MAKE) -C build stop
	./scripts/redis.sh stop

valgrind:
	$(MAKE) -C build valgrind NGINX_VERSION=$(NGINX_VERSION)\
	  SAGITTARIUS_CONFIG=$(SAGITTARIUS_CONFIG)

check:
	$(MAKE) -C build check
	$(MAKE) check-run check-stop

check-run:
	./test/run.sh

check-stop:
	$(MAKE) -C build check-stop
