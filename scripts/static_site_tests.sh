#!/bin/bash

# Static Site Tests for WebServer
# This script tests basic static website functionality

# --- Configuration ---
SERVER_URL="http://localhost:8090"
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# --- Helper Functions ---
print_header() {
    echo -e "\n=============================="
    echo -e "$1"
    echo -e "=============================="
}

# Function to run a test and check its status code
# Usage: run_test "Test Name" "Expected Status" "URL" [curl_options]
run_test() {
    local test_name="$1"
    local expected_status="$2"
    local url="$3"
    shift 3
    local curl_args=("$@")

    echo -e "\n--- ${test_name} ---"
    
    # Use -s for silent, -o to discard body, -w to get status code
    actual_status=$(curl -s -o /dev/null -w "%{http_code}" "${url}" "${curl_args[@]}")

    if [[ "$actual_status" == "$expected_status" ]]; then
        echo -e "[${GREEN}PASS${NC}] Expected ${expected_status}, Got ${actual_status}"
    else
        echo -e "[${RED}FAIL${NC}] Expected ${expected_status}, Got ${actual_status}"
    fi
}

echo "Starting Static Site Tests..."
echo "Make sure your server is running with 'server.conf'!"

# ============================================================================
# 1. Check main page and root path
# ============================================================================
print_header "1. Checking Main Page"
run_test "Main page (/index.html)" "200" "${SERVER_URL}/index.html"
run_test "Root path (/)" "200" "${SERVER_URL}/"

# ============================================================================
# 2. Check static assets are served correctly
# ============================================================================
print_header "2. Checking Static Assets"
run_test "CSS file" "200" "${SERVER_URL}/style.css"
run_test "JavaScript file" "200" "${SERVER_URL}/script.js"
run_test "Shell file" "200" "${SERVER_URL}/script.sh"
run_test "Python file" "200" "${SERVER_URL}/script.py"
run_test "PHP file" "200" "${SERVER_URL}/script.php"
run_test "Image file" "200" "${SERVER_URL}/cutecat.png"
run_test "File with spaces in name" "200" "${SERVER_URL}/my%20file.txt"
run_test "Gif file" "200" "${SERVER_URL}/blog/op.gif"

# ============================================================================
# 3. Check 404 for wrong URLs
# ============================================================================
print_header "3. Checking 404 Error Handling"
run_test "Non-existent HTML page" "404" "${SERVER_URL}/nonexistent-page.html"
run_test "Non-existent directory" "404" "${SERVER_URL}/nonexistent-directory/"
run_test "Non-existent asset" "404" "${SERVER_URL}/nonexistent.css"

# ============================================================================
# 4. Check 403 for Forbidden Access
# ============================================================================
print_header "4. Checking 403 Forbidden"
# This test requires a file like 'no_read_permissions.txt' to exist in the
# web root with its read permissions removed (e.g., using 'chmod 000').
run_test "File with no read permissions" "403" "${SERVER_URL}/no_read_permissions.txt"
# Note: Based on your conf, /forbidden is a directory with no index and autoindex off
run_test "Directory with autoindex off and no index" "403" "${SERVER_URL}/forbidden/"

# ============================================================================
# 5. Check directory listings and index files
# ============================================================================
print_header "5. Checking Directory Handling"
# Note: Based on your conf, /autoindex is the correct path for this test
run_test "Directory with autoindex on" "200" "${SERVER_URL}/autoindex/"
run_test "Directory with an index file" "200" "${SERVER_URL}/"

# ============================================================================
# 6. Check redirects
# ============================================================================
print_header "6. Checking Redirects"
# Note: this will not follow redirect. We check the initial status
run_test "Configured redirect - no follow redirect" "301" "${SERVER_URL}/redirect/" 
run_test "Directory without trailing slash (should redirect) - no follow redirect" "301" "${SERVER_URL}/autoindex"
# Note: -L tells curl to follow redirects. We check the final status.
run_test "Configured redirect - follow redirect" "200" "${SERVER_URL}/redirect/" -L
run_test "Directory without trailing slash (should redirect) - follow redirect" "200" "${SERVER_URL}/autoindex" -L

# ============================================================================
# 7. Checking Content Types
# ============================================================================
print_header "7. Checking Content Types"
# This section remains manual as it checks header content, not just status
echo -e "\n--- HTML content type ---"
curl -s -D - "${SERVER_URL}/index.html" -o /dev/null | grep -i "content-type"

echo -e "\n--- CSS content type ---"
curl -s -D - "${SERVER_URL}/style.css" -o /dev/null | grep -i "content-type"

echo -e "\n--- JS content type ---"
curl -s -D - "${SERVER_URL}/script.js" -o /dev/null | grep -i "content-type"

echo -e "\n--- SH content type ---"
curl -s -D - "${SERVER_URL}/script.sh" -o /dev/null | grep -i "content-type"

echo -e "\n--- PY content type ---"
curl -s -D - "${SERVER_URL}/script.py" -o /dev/null | grep -i "content-type"

echo -e "\n--- PHP content type ---"
curl -s -D - "${SERVER_URL}/script.php" -o /dev/null | grep -i "content-type"

echo -e "\n--- PNG content type ---"
curl -s -D - "${SERVER_URL}/cutecat.png" -o /dev/null | grep -i "content-type"

echo -e "\n--- GIF content type ---"
curl -s -D - "${SERVER_URL}/blog/op.gif" -o /dev/null | grep -i "content-type"

# ============================================================================
# 8. Testing Edge Cases
# ============================================================================
print_header "8. Testing Edge Cases"
LONG_URL="${SERVER_URL}/$(head -c 4000 < /dev/zero | tr '\0' 'a').html"
run_test "Very long URL" "414" "${LONG_URL}" # Expect 414 URI Too Long, or 404 if handled
run_test "URL with special characters" "200" "${SERVER_URL}/files/cat-css/cat.html"
run_test "Multiple slashes (should normalize to /)" "200" "${SERVER_URL}///"

echo -e "\n=========================================="
echo -e "Static Site Tests Completed!"
echo -e "=========================================="