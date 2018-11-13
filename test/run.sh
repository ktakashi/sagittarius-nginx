#!/bin/bash

set -e

tempfile=`mktemp`

check_status() {
    status=$1
    line=`head -n 1 $tempfile`
    if [[ $line =~ $status ]]; then
	return 0
    else
	echo 'Status not correct ' $status
	echo $line
	return -1
    fi
}

check_header() {
    name=$1
    value=$2
    # read until blank line and check
    return 0
}

echo "Test echo"
curl -i http://localhost:8080/echo > $tempfile
check_status '405'

curl -i http://localhost:8080/echo -d "string" > $tempfile
check_status '200' 
check_header 'Content-Type' 'text/plain'

echo 
echo "Test header"
curl -i http://localhost:8080/test-app > $tempfile

echo
echo "Test cookie"
curl -i http://localhost:8080/cookie -H "Cookie: key0=value0; key1=value1;" \
     -H "Cookie: key2=value2;" > $tempfile

echo $tempfile
rm $tempfile
