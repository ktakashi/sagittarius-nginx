FROM nginx
ARG sagittarius_version

RUN apt-get update
RUN apt-get install -y cmake libgc-dev zlib1g-dev \
	libffi-dev libssl-dev make gcc g++ curl

RUN mkdir /home/sagittarius
# Installing Sagittarius
ADD scripts/build-sagittarius.sh /home/sagittarius/build.sh
RUN cd /home/sagittarius && chmod +x ./build.sh
RUN cd /home/sagittarius && ./build.sh $sagittarius_version

# Installing libraries
ADD scripts/install-library.sh /home/sagittarius/install.sh
ADD scheme /home/sagittarius/src
RUN cd /home/sagittarius && chmod +x ./install.sh
RUN cd /home/sagittarius && ./install.sh src

# build NGINX module for sagittarius
RUN mkdir /home/sagittarius/nginx
ADD build/Makefile /home/sagittarius/nginx/Makefile
ADD src /home/sagittarius/nginx/src
RUN apt-get install -y libpcre3-dev
# get NGINX version of this docker
RUN nginx -v 2>&1 | sed -e 's|nginx version: nginx/||' > /home/sagittarius/nginx/version
RUN cat /home/sagittarius/nginx/version
RUN cd /home/sagittarius/nginx && \
	make NGINX_VERSION=`cat /home/sagittarius/nginx/version` \
	MODULE_LOCATION=../src
RUN cp /home/sagittarius/nginx/nginx-`cat /home/sagittarius/nginx/version`/objs/ngx_http_sagittarius_module.so \
	/usr/lib/nginx/modules/ngx_http_sagittarius_module.so
# make some required directories
RUN mkdir -p /etc/nginx/logs

CMD ["nginx", "-g", "daemon off;"]
