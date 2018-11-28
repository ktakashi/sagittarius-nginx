#!/bin/bash

set -e

tempfile=`mktemp`

check_status() {
    status=$1
    echo -n Status is $status ...
    
    line=`head -n 1 $tempfile`
    if [[ $line =~ $status ]]; then
	echo ok
	return 0
    else
	echo "not ok (actual $line)"
	return -1
    fi
}

check_header() {
    name=$1
    value=$2

    echo -n Header contains $name: $value ...
    while IFS= read -r line; do
	# echo $line
	if [[ $line =~ ^[[:space:]]*$ ]]
	then
	    echo not ok
	    return -1
	elif [[ $line =~ ^$name[[:space:]]*:[[:space:]]*$value[[:space:]]*$ ]]
	then
	    echo ok
	    return 0
	fi
    done < $tempfile
    # not found
    echo "not ok (invalid response)"
    return -1
}

check_content() {
    value=$1
    content=0

    echo -n Response contains $value ...
    while IFS='\r\n' read line; do
	if [[ $line =~ ^[[:space:]]*$ ]]; then
	    content=1
	fi
	if [ $content -eq 1 ]; then
	    if [[ $line =~ $value ]]; then
		echo ok
		return 0
	    fi
	fi
    done < $tempfile
    if [ x"$line" != x"" ]; then
	if [[ $line =~ $value ]]; then
	    echo ok
	    return 0
	fi
    fi
    echo not ok
    return -1
}

echo "Test echo"
curl -si http://localhost:8080/echo > $tempfile
check_status '405'

curl -si http://localhost:8080/echo -d "string" > $tempfile
check_status '200' 
check_header 'Content-Type' 'text/plain'
check_content 'string'

echo 
echo "Test header"
curl -si http://localhost:8080/test-app > $tempfile
check_status '200'
check_header "X-Sagittarius" [[:digit:]].[[:digit:]].[[:digit:]]
check_header "X-Context-Parameter" "value0"
check_header "X-Context-Parameter" "value1"
check_header "X-Context-Path" "/test-app"

echo 
echo "Test request"
# Fragment won't be sent via cURL
# see: https://curl.haxx.se/mail/lib-2011-11/0178.html
curl -si 'http://localhost:8080/test-app/acc?k1=v1&k2=v2#frag' > $tempfile
cat $tempfile
check_status '200'
check_content 'uri=/test-app/acc'
check_content 'query=k1=v1&k2=v2'
check_content 'original-uri=/test-app/acc\?k1=v1\&k2=v2'
check_content 'request-line=GET /test-app/acc\?k1=v1\&k2=v2 HTTP/1.1'

echo
echo "Test cookie"
curl -si http://localhost:8080/cookie -H "Cookie: key0=value0; key1=value1;" \
     -H "Cookie: key2=value2;" > $tempfile
check_content 'key0=value0'
check_content 'key1=value1'
check_content 'key2=value2'

# echo $tempfile
rm $tempfile
