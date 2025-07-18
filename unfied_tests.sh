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

echo "Starting Unified WebServer Tests..."
echo "Make sure your server is running with unified_server.conf!"

# =================== SETUP REQUIRED DIRECTORIES ===================
print_header "0. Creating Required Directories"
sudo mkdir -p /var/www/example.com
sudo mkdir -p /var/www/uploads
sudo mkdir -p /var/www/files
sudo mkdir -p /var/www/YoupiBanane
sudo mkdir -p /var/www/cgi-bin
sudo mkdir -p /var/www/empty
sudo mkdir -p /var/www/errors
sudo mkdir -p /var/www

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
print_header "3. Static File Serving"
print_test "CSS file"
curl -s -D - "$SERVER_URL/style.css" -o /dev/null
print_test "JS file"
curl -s -D - "$SERVER_URL/script.js" -o /dev/null
print_test "Image file"
curl -s -D - "$SERVER_URL/cutecat.png" -o /dev/null
print_test "File with spaces"
curl -s -D - "$SERVER_URL/my%20file.txt" -o /dev/null

# =================== AUTOINDEX & INDEX ===================
print_header "4. Directory Listings & Index"
print_test "Autoindex on"
curl -s -D - "$SERVER_URL/example.com/autoindex_on/" -o /dev/null
print_test "Autoindex off"
curl -s -D - "$SERVER_URL/example.com/autoindex_off_no_index/" -o /dev/null
print_test "Directory with index.html"
curl -s -D - "$SERVER_URL/example.com/" -o /dev/null

# =================== ERROR PAGES ===================
print_header "5. Error Pages"
print_test "404 error"
curl -s -D - "$SERVER_URL/nonexistent-page.html" -o /dev/null
print_test "403 error (protected file)"
curl -s -D - "$SERVER_URL/protected-file.txt" -o /dev/null
print_test "500 error (broken CGI)"
curl -s -D - "$SERVER_URL/cgi-bin/nonexistent.py" -o /dev/null

# =================== UPLOADS ===================
print_header "6. File Uploads"
echo "Text for upload test" > /tmp/test.txt
print_test "Text file upload"
out=$(curl -sv -F "file=@/tmp/test.txt" "$UPLOAD_URL" 2>&1)
check_status 201 "$out" "Text file upload"

print_test "Image upload"
out=$(curl -sv -F "file=@/var/www/files/cutecat.png" "$UPLOAD_URL" 2>&1)
check_status 201 "$out" "Image upload"

print_test "Binary file upload (.zip)"
zip -j /tmp/test.zip /tmp/test.txt
out=$(curl -sv -F "file=@/tmp/test.zip" "$UPLOAD_URL" 2>&1)
check_status 201 "$out" "Binary file upload (.zip)"

print_test "Upload file without extension"
cp /tmp/test.txt /tmp/noext
out=$(curl -sv -F "file=@/tmp/noext" "$UPLOAD_URL" 2>&1)
check_status 201 "$out" "Upload file without extension"

print_test "Upload file with special characters"
cp /tmp/test.txt "/tmp/a@b#c$.txt"
out=$(curl -sv -F "file=@/tmp/a@b#c$.txt" "$UPLOAD_URL" 2>&1)
check_status 201 "$out" "Upload file with special characters"

print_test "Upload larger than allowed (should return 413)"
dd if=/dev/zero of=/tmp/hugefile bs=1M count=100
out=$(curl -sv -F "file=@/tmp/hugefile" "$UPLOAD_URL" 2>&1)
check_status 413 "$out" "Upload larger than allowed"

print_test "Empty file upload"
touch /tmp/emptyfile
out=$(curl -sv -F "file=@/tmp/emptyfile" "$UPLOAD_URL" 2>&1)
check_status 201 "$out" "Empty file upload"

print_test "Incorrect Content-Type"
out=$(curl -sv -H "Content-Type: text/plain" --data-binary @/tmp/test.txt "$UPLOAD_URL" 2>&1)
check_status 415 "$out" "Incorrect Content-Type"

print_test "Missing boundary"
out=$(curl -sv -H "Content-Type: multipart/form-data" --data-binary @/tmp/test.txt "$UPLOAD_URL" 2>&1)
check_status 400 "$out" "Missing boundary"

print_test "Upload to non-existent directory (should return 404)"
out=$(curl -sv -F "file=@/tmp/test.txt" "$SERVER_URL/nonexistent/" 2>&1)
check_status 404 "$out" "Upload to non-existent directory"

print_test "Upload to directory without write permission (should return 403)"
mkdir -p /tmp/readonly_dir
chmod 555 /tmp/readonly_dir
out=$(curl -sv -F "file=@/tmp/test.txt" "$SERVER_URL/readonly/" 2>&1)
check_status 403 "$out" "Upload to directory without write permission"
chmod 755 /tmp/readonly_dir
rm -rf /tmp/readonly_dir

print_test "Concurrent uploads (should all succeed)"
echo "Starting 3 concurrent uploads..."
cp /tmp/test.txt /tmp/test1.txt
cp /tmp/test.txt /tmp/test2.txt
cp /tmp/test.txt /tmp/test3.txt
{
  curl -s -F "file=@/tmp/test1.txt" "$UPLOAD_URL" &
  curl -s -F "file=@/tmp/test2.txt" "$UPLOAD_URL" &
  curl -s -F "file=@/tmp/test3.txt" "$UPLOAD_URL" &
  wait
}
echo "[OK] Concurrent uploads finished (check server for all files)"
rm -f /tmp/test1.txt /tmp/test2.txt /tmp/test3.txt

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

# =================== REDIRECTS ===================
print_header "9. Redirects"
print_test "Redirect test"
curl -s -D - -L "$SERVER_URL/redirect/" -o /dev/null
print_test "Directory without trailing slash"
curl -s -D - -L "$SERVER_URL/example.com" -o /dev/null

# =================== CONTENT TYPES ===================
print_header "10. Content Types"
print_test "HTML content type"
curl -s -D - "$SERVER_URL/index.html" -o /dev/null | grep -i "content-type"
print_test "CSS content type"
curl -s -D - "$SERVER_URL/style.css" -o /dev/null | grep -i "content-type"
print_test "JS content type"
curl -s -D - "$SERVER_URL/script.js" -o /dev/null | grep -i "content-type"
print_test "PNG content type"
curl -s -D - "$SERVER_URL/cutecat.png" -o /dev/null | grep -i "content-type"

# =================== EDGE CASES ===================
print_header "11. Edge Cases"
print_test "Very long URL"
LONG_URL="$SERVER_URL/$(head -c 100 < /dev/zero | tr '\0' 'a').html"
curl -s -D - "$LONG_URL" -o /dev/null
print_test "Special characters in URL"
curl -s -D - "$SERVER_URL/files/cat-css/cat.html" -o /dev/null
print_test "Empty path"
curl -s -D - "$SERVER_URL" -o /dev/null
print_test "Multiple slashes"
curl -s -D - "$SERVER_URL///" -o /dev/null

# =================== CLEANUP ===================
rm -f /tmp/test.txt /tmp/test.zip /tmp/noext "/tmp/${LONGNAME}.txt" "/tmp/a@b#c$.txt" /tmp/dotfile /tmp/path_traversal.txt /tmp/hugefile /tmp/emptyfile "/tmp/${LONGNAME2}.txt"

echo -e "\n=========================================="
echo -e "All Unified Tests Completed!"
echo -e "==========================================" 