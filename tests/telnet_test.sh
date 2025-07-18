#!/bin/bash

# A small tester script using telnet to send a raw HTTP GET request.

# --- Configuration ---
HOST="localhost"
PORT="8090"
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo "--- Running Telnet Test ---"
echo "Connecting to ${HOST} on port ${PORT}..."

# --- The Test ---
# We use a subshell (...) to group the echo commands and pipe them to telnet.
# 'sleep 1' gives the server a moment to process and respond before the pipe closes.
# '2>/dev/null' suppresses telnet's connection status messages for cleaner output.
RESPONSE=$( (
    echo "GET / HTTP/1.1"
    echo "Host: ${HOST}"
    echo "Connection: close"
    echo "" # This final blank line is crucial to end the HTTP request
    sleep 1
) | telnet ${HOST} ${PORT} 2>/dev/null )

# --- Verification ---
# Check if the response contains the "200 OK" status line.
if echo "${RESPONSE}" | grep -q "HTTP/1.1 200 OK"; then
    echo -e "[${GREEN}PASS${NC}] Server responded with HTTP 200 OK."
    echo "--- Server Response Headers ---"
    # Print headers (lines until the first blank line)
    echo "${RESPONSE}" | sed '/^\r$/q'
    echo "-----------------------------"
    exit 0
else
    echo -e "[${RED}FAIL${NC}] Server did not respond with HTTP 200 OK."
    echo "--- Full Server Response ---"
    echo "${RESPONSE}"
    echo "--------------------------"
    exit 1
fi