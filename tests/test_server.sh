#!/bin/bash
# test_server.sh

HOST=localhost
PORT=8080

echo "Testing basic GET request..."
curl -v http://$HOST:$PORT/

echo "e\nTesting GET request with custom Host header..."
curl -v -H "Host: example.com" http://localhost:8080/

echo -e "\nTesting 404 path..."
curl -v http://$HOST:$PORT/not-found

echo -e "\nTesting POST request..."
curl -v -X POST -d "data=test" http://$HOST:$PORT/form