FROM nginx
ARG sagittarius_version

# Installing Sagittarius
ADD scripts/build-sagittarius.sh /home/sagittarius/build.sh
ADD scripts/install-library.sh /home/sagittarius/install.sh
ADD scheme /home/sagittarius/src
# build NGINX module for sagittarius
ADD build/Makefile /home/sagittarius/nginx/Makefile
ADD src /home/sagittarius/nginx/src

RUN apt-get update; \
    apt-get install --no-install-recommends --no-install-suggests -y \
	cmake libgc-dev zlib1g-dev ca-certificates \
	libffi-dev libssl-dev make gcc g++ curl \
 	libpcre3-dev; \
    mkdir -p /home/sagittarius/nginx; \
    cd /home/sagittarius && chmod +x ./build.sh && chmod +x ./install.sh; \
    cd /home/sagittarius && ./build.sh $sagittarius_version; \
    cd /home/sagittarius && ./install.sh src; \
    nginx -v 2>&1 | sed -e 's|nginx version: nginx/||' > /home/sagittarius/nginx/version; \
    cd /home/sagittarius/nginx && \
	make NGINX_VERSION=`cat /home/sagittarius/nginx/version` \
	MODULE_LOCATION=../src; \
    cp /home/sagittarius/nginx/nginx-`cat /home/sagittarius/nginx/version`/objs/ngx_http_sagittarius_module.so \
	/usr/lib/nginx/modules/ngx_http_sagittarius_module.so; \
    apt-get purge --auto-remove -y make cmake gcc g++ curl libpcre3-dev; \
    rm -rf /home/sagittarius; \
    rm -rf /var/lib/apt/lists/*

CMD ["nginx", "-g", "daemon off;"]
