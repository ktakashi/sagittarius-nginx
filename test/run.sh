#!/bin/bash

echo "Test echo"

curl -i http://localhost:8080/echo

curl -i http://localhost:8080/echo -d "string"

echo 
echo "Test header"

curl -i http://localhost:8080/test-app
