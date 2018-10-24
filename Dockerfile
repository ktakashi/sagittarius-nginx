FROM nginx
ARG sagittarius_version

RUN apt-get update
RUN apt-get install -y cmake libgc-dev zlib1g-dev \
	libffi-dev libssl-dev make gcc g++ curl

RUN mkdir /home/sagittarius
ADD scripts/build-sagittarius.sh /home/sagittarius/build.sh
RUN cd /home/sagittarius && chmod +x ./build.sh
RUN cd /home/sagittarius && ./build.sh $sagittarius_version
CMD ["/usr/local/bin/sash", "-i"]
