#!/bin/bash

# Unified Test Script for WebServer
# Combines config, static site, and upload tests

# =================== SETUP ENVIRONMENT ===================

# echo "Before running ensure the delete files were created"
# # For DELETE tests
# export DELETE_TEST_DIR="/var/www/delete"
# touch "$DELETE_TEST_DIR/deletable_file.txt"
# touch "$DELETE_TEST_DIR/protected_file.txt"
# touch "$DELETE_TEST_DIR/deletable_no_write_read_file.txt"
# chmod 000 "$DELETE_TEST_DIR/deletable_no_write_read_file.txt" # Read-only file
# chmod 444 "$DELETE_TEST_DIR/protected_file.txt" # Read-only file


# =================== CONFIGURATION ===================
MAIN_PORT="8090"
TEST_PORT="8082"
SERVER_URL="http://localhost:$MAIN_PORT"
UPLOAD_URL="$SERVER_URL/uploads/"
DELETE_TEST_DIR="/var/www/delete"
DELETE_URL="$SERVER_URL/delete"

set -e

print_header() {
    echo -e "\n=========================================="
    echo -e "$1"
    echo -e "=========================================="
}

print_test() {
    echo -e "\n--- $1 ---"
}

check_status() {
  local expected="$1"
  local output="$2"
  local msg="$3"
  local code=$(echo "$output" | grep -o "< HTTP/1.1 [0-9]*" | tail -1 | awk '{print $3}')
  if [[ "$code" == "$expected" ]]; then
    echo "[OK] $msg (HTTP $code)"
  else
    echo "[FAIL] $msg (HTTP $code, expected $expected)"
  fi
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

echo "Starting Unified WebServer Tests..."
echo "Make sure your server is running with unified_server.conf!"

#=================== BASIC SERVER TESTS ===================
print_header "1. Basic Server Reachability"
print_test "Main server root"
curl -s -D - "$SERVER_URL/" -o /dev/null
print_test "Test server root"
curl -s -D - "http://localhost:$TEST_PORT/" -o /dev/null

#=================== HOSTNAME TESTS ===================
print_header "2. Hostname Handling"
print_test "example.com hostname"
curl -s -D - --resolve "example.com:80:127.0.0.1" "$SERVER_URL/" -o /dev/null
print_test "test.com hostname"
curl -s -D - --resolve "test.com:80:127.0.0.1" "$SERVER_URL/" -o /dev/null
print_test "localhost hostname"
curl -s -D - "$SERVER_URL/" -o /dev/null

#=================== STATIC FILES ===================
print_header "3. Checking Static Files"
run_test "Main page (/index.html)" "200" "${SERVER_URL}/index.html"
run_test "Root path (/)" "200" "${SERVER_URL}/"
run_test "CSS file" "200" "${SERVER_URL}/style.css"
run_test "JavaScript file" "200" "${SERVER_URL}/script.js"
run_test "Shell file" "200" "${SERVER_URL}/script.sh"
run_test "Python file" "200" "${SERVER_URL}/script.py"
run_test "PHP file" "200" "${SERVER_URL}/script.php"
run_test "Image file" "200" "${SERVER_URL}/cutecat.png"
run_test "File with spaces in name" "200" "${SERVER_URL}/my%20file.txt"
run_test "Gif file" "200" "${SERVER_URL}/blog/op.gif"

#=================== 404 WRONG URLS ===================
print_header "4. Checking 404 Error Handling"
run_test "Non-existent HTML page" "404" "${SERVER_URL}/nonexistent-page.html"
run_test "Non-existent directory" "404" "${SERVER_URL}/nonexistent-directory/"
run_test "Non-existent asset" "404" "${SERVER_URL}/nonexistent.css"

#=================== 403 FORBIDDEN ACCESS ===================
print_header "5. Checking 403 Forbidden"
run_test "Directory with autoindex off and no index" "403" "${SERVER_URL}/forbidden/"

#=================== AUTO INDEX ===================
print_header "6. Checking Autoindex Directory Handling"
run_test "Directory with autoindex on" "200" "${SERVER_URL}/autoindex/"
run_test "Directory with an index file" "200" "${SERVER_URL}/"

#=================== REDIRECTS ===================
print_header "7. Checking Redirects"
# Note: this will not follow redirect. We check the initial status
run_test "Configured redirect - no follow redirect" "301" "${SERVER_URL}/redirect/" 
run_test "Directory without trailing slash (should redirect) - no follow redirect" "301" "${SERVER_URL}/autoindex"
# Note: -L tells curl to follow redirects. We check the final status.
run_test "Configured redirect - follow redirect" "200" "${SERVER_URL}/redirect/" -L
run_test "Directory without trailing slash (should redirect) - follow redirect" "200" "${SERVER_URL}/autoindex" -L

#=================== CONTENT TYPES ===================
print_header "8. Checking Content Types"
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

#=================== EDGE CASES ===================
print_header "9. Testing Edge Cases"
LONG_URL="${SERVER_URL}/$(head -c 4000 < /dev/zero | tr '\0' 'a').html"
run_test "Very long URL" "414" "${LONG_URL}" # Expect 414 URI Too Long, or 404 if handled
run_test "URL with special characters" "200" "${SERVER_URL}/files/cat-css/cat.html"
run_test "Multiple slashes (should normalize to /)" "200" "${SERVER_URL}///"

#=================== CLIENT BODY SIZE LIMIT ===================
print_header "10. Client Body Size Limit"

head -c 64 < /dev/zero | tr '\0' 'a' > /tmp/small_body.txt   # 1KB
head -c 101 < /dev/zero | tr '\0' 'a' > /tmp/large_body.txt # 

run_test "POST small body to CGI (should succeed)" "200" "$SERVER_URL/cgi-bin/python-cgi.py" -X POST -H "Content-Type: text/plain" --data-binary "@/tmp/small_body.txt"

# This test is failing
run_test "POST large body to CGI (should fail with 413)" "413" "$SERVER_URL/cgi-bin/python-cgi.py" -X POST -H "Content-Type: text/plain" --data-binary "@/tmp/large_body.txt"

#=================== FILE DELETION (DELETE) ===================
print_header "10. File Deletion (DELETE)"
run_test "Successful file deletion" "204" "${DELETE_URL}/deletable_file.txt" -X DELETE
run_test "Delete file in a non-writable directory" "403" "${DELETE_URL}/no_parent_write_perms/unreachable.txt" -X DELETE
run_test "Delete a non-existent file" "404" "${DELETE_URL}/non_existent_file.txt" -X DELETE
run_test "Attempt to delete a directory" "403" "${DELETE_URL}/a_directory/" -X DELETE
run_test "Method not allowed on DELETE location (GET)" "405" "${DELETE_URL}/" -X GET

# =================== UPLOADS ===================
print_header "11. File Uploads"
echo "Text for upload test" > /tmp/test.txt
run_test "Text file upload" "201" "$UPLOAD_URL" -F "file=@/tmp/test.txt"

run_test "Incorrect Content-Type" "415" "$UPLOAD_URL" -H "Content-Type: text/plain" --data-binary @/tmp/test.txt

run_test "Missing boundary" "400" "$UPLOAD_URL" -H "Content-Type: multipart/form-data" --data-binary @/tmp/test.txt

# =================== HTTP METHODS ===================
print_header "12. HTTP Methods"
run_test "GET on root (should succeed)" "200" "${SERVER_URL}/" -X GET
run_test "POST on root (should fail - method not allowed)" "405" "${SERVER_URL}/" -X POST
run_test "PUT on root (should fail - method not allowed)" "405" "${SERVER_URL}/" -X PUT
run_test "DELETE on root (should fail - method not allowed)" "405" "${SERVER_URL}/" -X DELETE

# =================== CGI ===================
print_header "13. CGI Scripts"
print_test "Python CGI"
curl -s -D - "$SERVER_URL/cgi-bin/python-cgi.py" -o /dev/null
print_test "Shell CGI"
curl -s -D - "$SERVER_URL/cgi-bin/shell-cgi.sh" -o /dev/null
print_test "CGI with params"
curl -s -D - "$SERVER_URL/cgi-bin/python-cgi.py?param1=value1&param2=value2" -o /dev/null

# =================== UNKNOWN REQUEST ===================
# telnet localhost 8090
# GIBBERISH / HTTP/1.1
# Host: example.com

# =================== CLEANUP ===================
rm -f /tmp/test.txt /tmp/test.zip /tmp/noext "/tmp/${LONGNAME}.txt" "/tmp/a@b#c$.txt" /tmp/dotfile /tmp/path_traversal.txt /tmp/hugefile /tmp/emptyfile "/tmp/${LONGNAME2}.txt"

echo -e "\n=========================================="
echo -e "All Unified Tests Completed!"
echo -e "==========================================" 


