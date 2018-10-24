SAGITTARIUS_VERSION=0.9.4
VERSION=0.0.1

all: 
	cd build && make

docker:
	docker build \
	-t sagittarius/nginx:$(VERSION) \
	--build-arg sagittarius_version=$(SAGITTARIUS_VERSION) \
	.

clean:
	cd build && make clean
