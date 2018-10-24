#!/bin/bash

SAGITTARIUS_VERSION=$1
BASE_URL=https://bitbucket.org/ktakashi/sagittarius-scheme/downloads

echo Downloading $BASE_URL/sagittarius-$SAGITTARIUS_VERSION.tar.gz

curl -kLo sagittarius.tar.gz $BASE_URL/sagittarius-$SAGITTARIUS_VERSION.tar.gz
tar xvf sagittarius.tar.gz
cd sagittarius-$SAGITTARIUS_VERSION
cmake .
make -j8
make install
