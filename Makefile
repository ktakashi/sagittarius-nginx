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
SAGITTARIUS_VERSION=0.9.4
NGINX_VERSION=1.15.5
VERSION=0.0.1

all: 
	$(MAKE) -C build NGINX_VERSION=$(NGINX_VERSION)\
	  SAGITTARIUS_CONFIG=$(SAGITTARIUS_CONFIG)

docker:
	docker build \
	-t sagittarius/nginx:$(VERSION) \
	--build-arg sagittarius_version=$(SAGITTARIUS_VERSION) \
	--build-arg host_nginx_version=$(NGINX_VERSION) \
	.

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
	$(MAKE) -C build run

stop:
	$(MAKE) -C build stop
