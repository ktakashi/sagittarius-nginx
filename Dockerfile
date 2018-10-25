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

ARG host_nginx_version
ADD build/nginx-$host_nginx_version/objs/ngx_http_sagittarius_module.so \
	/usr/lib/nginx/modules/ngx_http_sagittarius_module.so

CMD ["/usr/local/bin/sash", "-i"]
