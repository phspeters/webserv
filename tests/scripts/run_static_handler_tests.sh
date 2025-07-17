#!/bin/bash

# =============================================================================
# WEBSERV: STATIC FILE HANDLER AUTOMATED TEST SCRIPT
# =============================================================================
# Description:
# This script automates the tests defined in 'tests/httpyac/staticFileHandler.http'.
# It sets up a test directory, runs each test case against a running server,
# and reports the pass/fail status based on expected HTTP responses.
#
# Pre-requisites:
# 1. The 'webserv' server must be running.
# 2. The server must be configured to serve files from the 'tests/test_www/static_test_root'
#    directory for the specified port. Example for valid.conf:
#
#    location / {
#        root tests/test_www/static_test_root;
#        index index.html;
#        allow_methods GET;
#        autoindex on; # Enable autoindex for testing
#    }
#    location /old-page.html {
#        redirect 301 /new-page.html;
#    }
# =============================================================================

# --- Configuration ---
HOST="localhost"
PORT="8090"
BASE_URL="http://${HOST}:${PORT}"
TEST_ROOT="tests/test_www/static_test_root"
USER_AGENT="WebServ-Test-Client/1.0"

# --- Colors for Output ---
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# --- Counters ---
PASSED_COUNT=0
FAILED_COUNT=0

# --- Helper Functions ---

# Function to set up the required directory and file structure for tests
setup_test_environment() {
    echo -e "${YELLOW}Setting up test environment in '${TEST_ROOT}'...${NC}"
    rm -rf "${TEST_ROOT}"
    mkdir -p "${TEST_ROOT}/subdir"
    mkdir -p "${TEST_ROOT}/autoindex_on"
    mkdir -p "${TEST_ROOT}/autoindex_off_no_index"

    # Create test files
    echo "<h1>Index Page</h1>" > "${TEST_ROOT}/index.html"
    echo "body { color: blue; }" > "${TEST_ROOT}/style.css"
    echo "console.log('hello world');" > "${TEST_ROOT}/script.js"
    echo "PNG_DATA" > "${TEST_ROOT}/cutecat.png"
    echo "<h1>Subdir Page</h1>" > "${TEST_ROOT}/subdir/page.html"
    echo "file1" > "${TEST_ROOT}/autoindex_on/file1.txt"
    echo "file2" > "${TEST_ROOT}/autoindex_on/file2.txt"
    echo "secret" > "${TEST_ROOT}/autoindex_off_no_index/secret.txt"
    echo "This is a new page" > "${TEST_ROOT}/new-page.html"
    echo "a file with spaces" > "${TEST_ROOT}/my file.txt"
    echo "hidden" > "${TEST_ROOT}/.hiddenfile"

    # Create a file with no read permissions
    echo "permission denied" > "${TEST_ROOT}/no_read_permissions.txt"
    chmod 000 "${TEST_ROOT}/no_read_permissions.txt"

    echo "Setup complete."
    echo "--------------------------------------------------"
}

# Function to run a single test case
run_test() {
    local test_name="$1"
    local expected_status="$2"
    local expected_header_pattern="$3" # e.g., "Location: /subdir/"
    shift 3
    local curl_args=("$@")

    # Use -i to get headers, -s for silent, -w for status code
    response_headers=$(curl -s -i "${curl_args[@]}" 2>&1)
    http_status=$(echo "$response_headers" | grep -oE '^HTTP/[0-9\.]+ [0-9]+' | awk '{print $2}' | tail -n 1)

    # Check status code
    if [[ "$http_status" == "$expected_status" ]]; then
        status_ok=true
    else
        status_ok=false
    fi

    # Check for expected header if provided
    header_ok=true
    if [[ -n "$expected_header_pattern" ]]; then
        # Use grep with -i for case-insensitive header matching
        if ! echo "$response_headers" | grep -iqE "^${expected_header_pattern}"; then
            header_ok=false
        fi
    fi

    # Report result
    if $status_ok && $header_ok; then
        echo -e "[${GREEN}PASS${NC}] ${test_name} (Expected: ${expected_status}, Got: ${http_status})"
        ((PASSED_COUNT++))
    else
        echo -e "[${RED}FAIL${NC}] ${test_name}"
        echo -e "  - Expected Status: ${expected_status}, Got: ${http_status}"
        if [[ -n "$expected_header_pattern" ]]; then
            echo -e "  - Expected Header Pattern: '${expected_header_pattern}' (Not found or incorrect)"
        fi
        ((FAILED_COUNT++))
    fi
}

# Function to clean up the test environment
# cleanup() {
#     echo "--------------------------------------------------"
#     echo -e "${YELLOW}Cleaning up test environment...${NC}"
#     # Restore permissions to allow deletion
#     chmod 644 "${TEST_ROOT}/no_read_permissions.txt" 2>/dev/null
#     rm -rf "${TEST_ROOT}"
#     echo "Cleanup complete."
# }

# --- Main Script Execution ---

# Ensure cleanup happens on script exit (e.g., Ctrl+C)
# trap cleanup EXIT

setup_test_environment

echo -e "${YELLOW}Section 1: Basic File Retrieval (200 OK)${NC}"
run_test "Test 1.1: Get root index file" "200" "" -X GET "${BASE_URL}/index.html" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 1.2: Get a CSS file" "200" "" -X GET "${BASE_URL}/style.css" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 1.3: Get a JavaScript file" "200" "" -X GET "${BASE_URL}/script.js" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 1.4: Get a PNG image" "200" "" -X GET "${BASE_URL}/cutecat.png" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 1.5: Get a file from a subdirectory" "200" "" -X GET "${BASE_URL}/subdir/page.html" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"

echo -e "\n${YELLOW}Section 2: Directory Handling${NC}"
run_test "Test 2.1: Request directory root with implicit index" "200" "" -X GET "${BASE_URL}/" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 2.2: Request subdirectory without trailing slash" "301" "Location: /subdir/" -X GET "${BASE_URL}/subdir" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 2.3: Request subdirectory with trailing slash" "200" "" -X GET "${BASE_URL}/subdir/" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 2.4: Directory with autoindex ON" "200" "" -X GET "${BASE_URL}/autoindex_on/" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 2.5: Directory with autoindex OFF and no index file" "403" "" -X GET "${BASE_URL}/autoindex_off_no_index/" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"

echo -e "\n${YELLOW}Section 3: Error Handling (4xx Status Codes)${NC}"
run_test "Test 3.1: File Not Found" "404" "" -X GET "${BASE_URL}/non_existent_file.html" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 3.2: Directory Not Found" "404" "" -X GET "${BASE_URL}/non_existent_directory/" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 3.3: File with No Read Permissions" "403" "" -X GET "${BASE_URL}/no_read_permissions.txt" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 3.4: Path Traversal Attempt" "404" "" -X GET "${BASE_URL}/../../../../etc/passwd" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 3.5: Method Not Allowed (POST)" "405" "Allow: GET" -X POST "${BASE_URL}/index.html" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}" -d "test"
run_test "Test 3.6: Method Not Allowed (DELETE)" "405" "Allow: GET" -X DELETE "${BASE_URL}/index.html" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"

echo -e "\n${YELLOW}Section 4: Redirects${NC}"
run_test "Test 4.1: Location-level Redirect" "301" "Location: /new-page.html" -X GET "${BASE_URL}/old-page.html" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 4.2: Directory Trailing Slash Redirect with Query String" "301" "Location: /subdir/?param=value" -X GET "${BASE_URL}/subdir?param=value" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"

echo -e "\n${YELLOW}Section 5: Edge Cases${NC}"
run_test "Test 5.1: Request with URL-encoded characters" "200" "" -X GET "${BASE_URL}/%73%74%79%6c%65%2e%63%73%73" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 5.2: Request for a file with a space in the name" "200" "" -X GET "${BASE_URL}/my%20file.txt" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}"
run_test "Test 5.3: Request for a hidden file (dotfile)" "200" "" -X GET "${BASE_URL}/.hiddenfile" -H "Host: example.com" -H "User-Agent: ${USER_AGENT}" # Expect 404 as servers usually ignore dotfiles

# --- Final Summary ---
echo "--------------------------------------------------"
echo -e "${YELLOW}Test Summary:${NC}"
echo -e "  - ${GREEN}Passed: ${PASSED_COUNT}${NC}"
echo -e "  - ${RED}Failed: ${FAILED_COUNT}${NC}"
echo "--------------------------------------------------"

# Exit with an error code if any tests failed
if [ "$FAILED_COUNT" -gt 0 ]; then
    exit 1
fi

exit 0