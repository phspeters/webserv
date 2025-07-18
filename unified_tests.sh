#!/bin/bash

# Unified Test Script for WebServer
# Combines config, static site, and upload tests

# =================== CONFIGURATION ===================
MAIN_PORT="8090"
TEST_PORT="8082"
SERVER_URL="http://localhost:$MAIN_PORT"
UPLOAD_URL="$SERVER_URL/uploads/"

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

# =================== SETUP REQUIRED DIRECTORIES ===================
print_header "0. Creating Required Directories"
sudo mkdir -p /var/www
sudo mkdir -p /var/www/example.com
sudo mkdir -p /var/www/YoupiBanane
sudo mkdir -p /var/www/uploads
sudo mkdir -p /var/www/files
sudo mkdir -p /var/www/cgi-bin
sudo mkdir -p /var/www/empty
sudo mkdir -p /var/www/errors
sudo mkdir -p /var/www/autoindex
sudo mkdir -p /var/www/blog
sudo mkdir -p /var/www/forbidden
sudo mkdir -p /var/www/uploads

# Create some static files for testing
sudo bash -c 'echo "body { color: blue; }" > /var/www/style.css'
sudo bash -c 'echo "console.log(\"hello world\");" > /var/www/script.js'
sudo bash -c 'echo "<h1>Index Page</h1>" > /var/www/index.html'
sudo bash -c 'echo "test file with spaces" > "/var/www/my file.txt"'
sudo bash -c 'echo "PNG_DATA" > /var/www/cutecat.png'

# For /var/www/files/cat-css/cat.html (used in redirect)
sudo mkdir -p /var/www/files/cat-css
sudo bash -c 'echo "<h1>Cat HTML</h1>" > /var/www/files/cat-css/cat.html'
sudo bash -c 'echo "cat css" > /var/www/files/cat-css/styles.css'
sudo bash -c 'echo "cutecat" > /var/www/files/cutecat.png'
sudo bash -c 'echo "text file" > /var/www/files/text.txt'
sudo bash -c 'echo "file manager" > /var/www/files/file-manager.html'
sudo bash -c 'echo "index" > /var/www/files/index.html'

# For /var/www/example.com
sudo bash -c 'echo "<h1>Example Index</h1>" > /var/www/example.com/index.html'
sudo mkdir -p /var/www/example.com/autoindex_on
sudo bash -c 'echo "file1" > /var/www/example.com/autoindex_on/file1.txt'
sudo bash -c 'echo "file2" > /var/www/example.com/autoindex_on/file2.txt'
sudo mkdir -p /var/www/example.com/autoindex_off_no_index
sudo bash -c 'echo "secret" > /var/www/example.com/autoindex_off_no_index/secret.txt'
sudo mkdir -p /var/www/example.com/blogs
sudo bash -c 'echo "<h1>Blogs</h1>" > /var/www/example.com/blogs/op.gif'
sudo bash -c 'echo "new page" > /var/www/example.com/new-page.html'
sudo bash -c 'echo "index" > /var/www/example.com/index.html'
sudo bash -c 'echo "script" > /var/www/example.com/script.js'
sudo bash -c 'echo "style" > /var/www/example.com/style.css'
sudo mkdir -p /var/www/example.com/subdir
sudo bash -c 'echo "page" > /var/www/example.com/subdir/page.html'

# For /var/www/YoupiBanane
sudo bash -c 'echo "<h1>YoupiBanane Index</h1>" > /var/www/YoupiBanane/index.html'

# For /var/www/cgi-bin
sudo bash -c 'echo "#!/usr/bin/env python3\nprint(\"Content-Type: text/html\\n\\nHello from Python CGI!\")" > /var/www/cgi-bin/python-cgi.py'
sudo chmod +x /var/www/cgi-bin/python-cgi.py
sudo bash -c 'echo "#!/bin/sh\necho Content-Type: text/html\necho\necho Hello from Shell CGI!" > /var/www/cgi-bin/shell-cgi.sh'
sudo chmod +x /var/www/cgi-bin/shell-cgi.sh

# For /var/www/errors
sudo bash -c 'echo "<h1>404 Not Found</h1>" > /var/www/errors/404_custom.html'
sudo bash -c 'echo "<h1>500 Internal Server Error</h1>" > /var/www/errors/500_custom.html'

# Setup for DELETE tests
echo "Setting up environment for DELETE tests..."

# Create directories for DELETE tests
DELETE_TEST_DIR="/var/www/delete"
sudo rm -rf "$DELETE_TEST_DIR"
sudo mkdir -p "$DELETE_TEST_DIR/a_directory"
sudo mkdir -p "$DELETE_TEST_DIR/no_parent_write_perms"

# Create files for DELETE tests
sudo bash -c 'echo "This file should be deleted." > '"$DELETE_TEST_DIR/deletable_file.txt"
sudo bash -c 'echo "This file is read-only." > '"$DELETE_TEST_DIR/protected_file.txt"
sudo bash -c 'echo "This file cannot be deleted." > '"$DELETE_TEST_DIR/no_parent_write_perms/unreachable.txt"

# Set special permissions for DELETE tests
sudo chmod 444 "$DELETE_TEST_DIR/protected_file.txt" # Read-only file
sudo chmod 555 "$DELETE_TEST_DIR/no_parent_write_perms" # Parent directory is not writable

# Assumes a location like '/delete/' is configured to allow DELETE method.
DELETE_URL="${SERVER_URL}/delete"

# =================== BASIC SERVER TESTS ===================
print_header "1. Basic Server Reachability"
print_test "Main server root"
curl -s -D - "$SERVER_URL/" -o /dev/null
print_test "Test server root"
curl -s -D - "http://localhost:$TEST_PORT/" -o /dev/null

# =================== HOSTNAME TESTS ===================
print_header "2. Hostname Handling"
print_test "example.com hostname"
curl -s -D - --resolve "example.com:80:127.0.0.1" "$SERVER_URL/" -o /dev/null
print_test "test.com hostname"
curl -s -D - --resolve "test.com:80:127.0.0.1" "$SERVER_URL/" -o /dev/null
print_test "localhost hostname"
curl -s -D - "$SERVER_URL/" -o /dev/null

# =================== STATIC FILES ===================
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

# =================== 404 WRONG URLS ===================
print_header "4. Checking 404 Error Handling"
run_test "Non-existent HTML page" "404" "${SERVER_URL}/nonexistent-page.html"
run_test "Non-existent directory" "404" "${SERVER_URL}/nonexistent-directory/"
run_test "Non-existent asset" "404" "${SERVER_URL}/nonexistent.css"

# =================== 403 FORBIDDEN ACCESS ===================
print_header "5. Checking 403 Forbidden"
# This test requires a file like 'no_read_permissions.txt' to exist in the
# web root with its read permissions removed (e.g., using 'chmod 000').
run_test "File with no read permissions" "403" "${SERVER_URL}/no_read_permissions.txt"
# Note: Based on your conf, /forbidden is a directory with no index and autoindex off
run_test "Directory with autoindex off and no index" "403" "${SERVER_URL}/forbidden/"

# =================== AUTO INDEX ===================
print_header "6. Checking Autoindex Directory Handling"
# Note: Based on your conf, /autoindex is the correct path for this test
run_test "Directory with autoindex on" "200" "${SERVER_URL}/autoindex/"
run_test "Directory with an index file" "200" "${SERVER_URL}/"

# =================== REDIRECTS ===================
print_header "7. Checking Redirects"
# Note: this will not follow redirect. We check the initial status
run_test "Configured redirect - no follow redirect" "301" "${SERVER_URL}/redirect/" 
run_test "Directory without trailing slash (should redirect) - no follow redirect" "301" "${SERVER_URL}/autoindex"
# Note: -L tells curl to follow redirects. We check the final status.
run_test "Configured redirect - follow redirect" "200" "${SERVER_URL}/redirect/" -L
run_test "Directory without trailing slash (should redirect) - follow redirect" "200" "${SERVER_URL}/autoindex" -L

# =================== CONTENT TYPES ===================
print_header "8. Checking Content Types"
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

# =================== EDGE CASES ===================
print_header "9. Testing Edge Cases"
LONG_URL="${SERVER_URL}/$(head -c 4000 < /dev/zero | tr '\0' 'a').html"
run_test "Very long URL" "414" "${LONG_URL}" # Expect 414 URI Too Long, or 404 if handled
run_test "URL with special characters" "200" "${SERVER_URL}/files/cat-css/cat.html"
run_test "Multiple slashes (should normalize to /)" "200" "${SERVER_URL}///"

# =================== FILE DELETION (DELETE) ===================
print_header "7. File Deletion (DELETE)"
run_test "Successful file deletion" "204" "${DELETE_URL}/deletable_file.txt" -X DELETE
run_test "Delete a read-only file (should succeed)" "204" "${DELETE_URL}/protected_file.txt" -X DELETE
run_test "Delete a non-existent file" "404" "${DELETE_URL}/non_existent_file.txt" -X DELETE
run_test "Attempt to delete a directory" "403" "${DELETE_URL}/a_directory/" -X DELETE
run_test "Delete file in a non-writable directory" "403" "${DELETE_URL}/no_parent_write_perms/unreachable.txt" -X DELETE
run_test "Path traversal delete attempt" "404" "${DELETE_URL}/../../../../etc/passwd" -X DELETE
run_test "Method not allowed on DELETE location (GET)" "405" "${DELETE_URL}/" -X GET

# =================== UPLOADS ===================
print_header "6. File Uploads"
echo "Text for upload test" > /tmp/test.txt
run_test "Text file upload" "201" "$UPLOAD_URL" -F "file=@/tmp/test.txt"

run_test "Image upload" "201" "$UPLOAD_URL" -F "file=@/var/www/files/cutecat.png"

zip -j /tmp/test.zip /tmp/test.txt
run_test "Binary file upload (.zip)" "201" "$UPLOAD_URL" -F "file=@/tmp/test.zip"

cp /tmp/test.txt /tmp/noext
run_test "Upload file without extension" "201" "$UPLOAD_URL" -F "file=@/tmp/noext"

cp /tmp/test.txt "/tmp/a@b#c$.txt"
run_test "Upload file with special characters" "201" "$UPLOAD_URL" -F "file=@/tmp/a@b#c$.txt"

# Problematic test
# dd if=/dev/zero of=/tmp/hugefile bs=1M count=100
# run_test "Upload larger than allowed" "413" "$UPLOAD_URL" -F "file=@/tmp/hugefile"

touch /tmp/emptyfile
run_test "Empty file upload" "201" "$UPLOAD_URL" -F "file=@/tmp/emptyfile"

run_test "Incorrect Content-Type" "415" "$UPLOAD_URL" -H "Content-Type: text/plain" --data-binary @/tmp/test.txt

run_test "Missing boundary" "400" "$UPLOAD_URL" -H "Content-Type: multipart/form-data" --data-binary @/tmp/test.txt

run_test "Upload to non-existent directory (should return 404)" "404" "$SERVER_URL/nonexistent/" -F "file=@/tmp/test.txt"

# To do: check this test
# mkdir -p /tmp/readonly_dir
# chmod 555 /tmp/readonly_dir
# run_test "Upload to directory without write permission" "403" "$SERVER_URL/readonly/" -F "file=@/tmp/test.txt"
# chmod 755 /tmp/readonly_dir
# rm -rf /tmp/readonly_dir

# =================== HTTP METHODS ===================
print_header "7. HTTP Methods"
print_test "GET root"
curl -X GET -s -D - "$SERVER_URL/" -o /dev/null
print_test "POST root"
curl -X POST -s -D - "$SERVER_URL/" -o /dev/null
print_test "DELETE root (should fail)"
curl -X DELETE -s -D - "$SERVER_URL/" -o /dev/null
print_test "PUT root (should fail)"
curl -X PUT -s -D - "$SERVER_URL/" -o /dev/null
print_test "DELETE on /delete route"
curl -X DELETE -s -D - "$SERVER_URL/delete/deletable_file.txt" -o /dev/null
print_test "DELETE on protected file (should fail)"
curl -X DELETE -s -D - "$SERVER_URL/delete/protected_file.txt" -o /dev/null
print_test "DELETE on file without parent write permissions"
curl -X DELETE -s -D - "$SERVER_URL/delete/no_parent_write_perms/unreachable.txt" -o /dev/null

# =================== CGI ===================
print_header "8. CGI Scripts"
print_test "Python CGI"
curl -s -D - "$SERVER_URL/cgi-bin/python-cgi.py" -o /dev/null
print_test "Shell CGI"
curl -s -D - "$SERVER_URL/cgi-bin/shell-cgi.sh" -o /dev/null
print_test "CGI with params"
curl -s -D - "$SERVER_URL/cgi-bin/python-cgi.py?param1=value1&param2=value2" -o /dev/null

# =================== CLEANUP ===================
rm -f /tmp/test.txt /tmp/test.zip /tmp/noext "/tmp/${LONGNAME}.txt" "/tmp/a@b#c$.txt" /tmp/dotfile /tmp/path_traversal.txt /tmp/hugefile /tmp/emptyfile "/tmp/${LONGNAME2}.txt"

echo -e "\n=========================================="
echo -e "All Unified Tests Completed!"
echo -e "==========================================" 

