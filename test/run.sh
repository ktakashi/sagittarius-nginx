#!/bin/bash

echo "Test echo"

curl -i http://localhost:8080/echo
curl -i http://localhost:8080/echo -d "string"

echo 
echo "Test header"
curl -i http://localhost:8080/test-app

echo
echo "Test cookie"
curl -i http://localhost:8080/cookie -H "Cookie: key0=value0; key1=value1;" \
     -H "Cookie: key2=value2;"
