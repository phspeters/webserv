#!/bin/bash

# Configuration Tests for WebServer
# This script tests various server configuration scenarios
# Should be run with the multi_server.conf file

print_header() {
    echo -e "\n=========================================="
    echo -e "$1"
    echo -e "=========================================="
}

print_test() {
    echo -e "\n--- $1 ---"
}

# Configuration
SERVER1_PORT="8080"
SERVER2_PORT="8081"
SERVER3_PORT="8082"

echo "Starting Configuration Tests..."
echo "Make sure your servers are running with the appropriate configs!"

# ============================================================================
# 1. Multiple servers with different ports
# ============================================================================
print_header "1. Testing Multiple Servers with Different Ports"

print_test "Testing server on port $SERVER1_PORT"
curl -s -D - "http://localhost:$SERVER1_PORT/" -o /dev/null

print_test "Testing server on port $SERVER2_PORT"
curl -s -D - "http://localhost:$SERVER2_PORT/" -o /dev/null

print_test "Testing server on port $SERVER3_PORT"
curl -s -D - "http://localhost:$SERVER3_PORT/" -o /dev/null

# ============================================================================
# 2. Multiple servers with different hostnames
# ============================================================================
print_header "2. Testing Multiple Servers with Different Hostnames"

print_test "Testing example.com hostname"
curl -s -D - --resolve "example.com:80:127.0.0.1" "http://example.com/" -o /dev/null

print_test "Testing test.com hostname"
curl -s -D - --resolve "test.com:80:127.0.0.1" "http://test.com/" -o /dev/null

print_test "Testing localhost hostname"
curl -s -D - "http://localhost/" -o /dev/null

# ============================================================================
# 3. Default error pages
# ============================================================================
print_header "3. Testing Default Error Pages"

print_test "Testing 404 error page"
curl -s -D - "http://localhost:$SERVER1_PORT/nonexistent-page.html" -o /dev/null

print_test "Testing 403 error page (if applicable)"
curl -s -D - "http://localhost:$SERVER1_PORT/protected-file.txt" -o /dev/null

print_test "Testing 500 error page (if applicable)"
curl -s -D - "http://localhost:$SERVER1_PORT/cgi-bin/broken-script.py" -o /dev/null

# ============================================================================
# 4. Client body size limits
# ============================================================================
print_header "4. Testing Client Body Size Limits"

print_test "Testing with small body (should work)"
curl -X POST -H "Content-Type: text/plain" \
     --data "Small body content" \
     "http://localhost:$SERVER1_PORT/upload" \
     -w "\nHTTP Status: %{http_code}\n" -s

print_test "Testing with medium body (should work)"
curl -X POST -H "Content-Type: text/plain" \
     --data "$(head -c 1000 < /dev/zero | tr '\0' 'A')" \
     "http://localhost:$SERVER1_PORT/upload" \
     -w "\nHTTP Status: %{http_code}\n" -s

print_test "Testing with large body (should fail with 413)"
curl -X POST -H "Content-Type: text/plain" \
     --data "$(head -c 1000000 < /dev/zero | tr '\0' 'A')" \
     "http://localhost:$SERVER1_PORT/upload" \
     -w "\nHTTP Status: %{http_code}\n" -s

# ============================================================================
# 5. Routes to different directories
# ============================================================================
print_header "5. Testing Routes to Different Directories"

print_test "Testing root route /"
curl -s -D - "http://localhost:$SERVER1_PORT/" -o /dev/null

print_test "Testing /files route"
curl -s -D - "http://localhost:$SERVER1_PORT/files/" -o /dev/null

print_test "Testing /example.com route"
curl -s -D - "http://localhost:$SERVER1_PORT/example.com/" -o /dev/null

print_test "Testing /YoupiBanane route"
curl -s -D - "http://localhost:$SERVER1_PORT/YoupiBanane/" -o /dev/null

# ============================================================================
# 6. Default files for directories
# ============================================================================
print_header "6. Testing Default Files for Directories"

print_test "Testing directory with index.html"
curl -s -D - "http://localhost:$SERVER1_PORT/example.com/" -o /dev/null

print_test "Testing directory with autoindex on"
curl -s -D - "http://localhost:$SERVER1_PORT/example.com/autoindex_on/" -o /dev/null

print_test "Testing directory with autoindex off"
curl -s -D - "http://localhost:$SERVER1_PORT/example.com/autoindex_off_no_index/" -o /dev/null

# ============================================================================
# 7. HTTP Methods for routes
# ============================================================================
print_header "7. Testing HTTP Methods for Routes"

print_test "Testing GET method (should work)"
curl -X GET -s -D - "http://localhost:$SERVER1_PORT/" -o /dev/null

print_test "Testing POST method (if allowed)"
curl -X POST -s -D - "http://localhost:$SERVER1_PORT/" -o /dev/null

print_test "Testing DELETE method (should fail if not allowed)"
curl -X DELETE -s -D - "http://localhost:$SERVER1_PORT/" -o /dev/null

print_test "Testing PUT method (should fail if not allowed)"
curl -X PUT -s -D - "http://localhost:$SERVER1_PORT/" -o /dev/null

print_test "Testing DELETE on specific route (if configured)"
curl -X DELETE -s -D - "http://localhost:$SERVER1_PORT/delete/" -o /dev/null

# ============================================================================
# 8. File operations with permissions
# ============================================================================
print_header "8. Testing File Operations with Permissions"

print_test "Testing DELETE on deletable file"
curl -X DELETE -s -D - "http://localhost:$SERVER1_PORT/delete/deletable_file.txt" -o /dev/null

print_test "Testing DELETE on protected file (should fail)"
curl -X DELETE -s -D - "http://localhost:$SERVER1_PORT/delete/protected_file.txt" -o /dev/null

print_test "Testing DELETE on file without parent write permissions"
curl -X DELETE -s -D - "http://localhost:$SERVER1_PORT/delete/no_parent_write_perms/unreachable.txt" -o /dev/null

# ============================================================================
# 9. CGI Testing
# ============================================================================
print_header "9. Testing CGI Functionality"

print_test "Testing Python CGI script"
curl -s -D - "http://localhost:$SERVER1_PORT/cgi-bin/python-cgi.py" -o /dev/null

print_test "Testing Shell CGI script"
curl -s -D - "http://localhost:$SERVER1_PORT/cgi-bin/shell-cgi.sh" -o /dev/null

print_test "Testing CGI with parameters"
curl -s -D - "http://localhost:$SERVER1_PORT/cgi-bin/python-cgi.py?param1=value1&param2=value2" -o /dev/null

# ============================================================================
# 10. Edge Cases
# ============================================================================
print_header "10. Testing Edge Cases"

print_test "Testing very long URL"
LONG_URL="http://localhost:$SERVER1_PORT/$(head -c 200 < /dev/zero | tr '\0' 'a').html"
curl -s -D - "$LONG_URL" -o /dev/null

print_test "Testing URL with special characters"
curl -s -D - "http://localhost:$SERVER1_PORT/files/cat-css/cat.html" -o /dev/null

print_test "Testing file with spaces in name"
curl -s -D - "http://localhost:$SERVER1_PORT/my%20file.txt" -o /dev/null

print_test "Testing non-existent CGI script"
curl -s -D - "http://localhost:$SERVER1_PORT/cgi-bin/nonexistent.py" -o /dev/null

echo -e "\n=========================================="
echo -e "All Configuration Tests Completed!"
echo -e "=========================================="
