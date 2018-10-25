#!/bin/bash

SRC_DIR=$1
TMP=`/usr/local/bin/sagittarius-config --sharedir`
SHARED_DIR="$(echo -e "${TMP}" | tr -d '[:space:]')"

echo Installing $SRC_DIR to $SHARED_DIR
cp -r $SRC_DIR/sagittarius $SHARED_DIR/sitelib
