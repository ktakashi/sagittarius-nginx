# This works only on 0.9.5...
SAGITTARIUS_PREFIX=$(shell sagittarius-config --install-prefix)
SAGITTARIUS_PKGDIR=$(shell sagittarius-config --pkglibdir)
SAGITTARIUS_SITELIB=$(SAGITTARIUS_PREFIX)/$(SAGITTARIUS_PKGDIR)

# docker info
SAGITTARIUS_VERSION=0.9.4
NGINX_VERSION=1.15.5
VERSION=0.0.1

# command
RM        ?= rm -f
INSTALL   ?= install
MKDIR     ?= $(INSTALL) -d

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

install:
	$(MAKE) -C build NGINX_VERSION=$(NGINX_VERSION) install
	$(MKDIR) $(DESTDIR)$(SAGITTARIUS_SITELIB)/sagittarius
	$(INSTALL) -m0644 scheme/sagittarius/*.scm $(DESTDIR)$(SAGITTARIUS_SITELIB)/sagittarius

# nginx doesn't provide uninstall target. sad...
uninstall:
	$(RM) $(DESTDIR)$(SAGITTARIUS_SITELIB)/sagittarius
