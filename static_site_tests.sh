#!/bin/bash

# Static Site Tests for WebServer
# This script tests basic static website functionality

print_header() {
    echo -e "\n=============================="
    echo -e "$1"
    echo -e "=============================="
}

print_test() {
    echo -e "\n--- $1 ---"
}

# Configuration
SERVER_URL="http://localhost:8090"

echo "Starting Static Site Tests..."
echo "Make sure your server is running!"

# ============================================================================
# 1. Check headers for main page
# ============================================================================
print_header "1. Checking Headers for Main Page"
print_test "Main page headers (/index.html)"
curl -s -D - "$SERVER_URL/index.html" -o /dev/null

print_test "Root path headers (/)"
curl -s -D - "$SERVER_URL/" -o /dev/null

# ============================================================================
# 2. Check static assets are served correctly
# ============================================================================
print_header "2. Checking Static Assets"
print_test "CSS file"
curl -s -D - "$SERVER_URL/style.css" -o /dev/null

print_test "JavaScript file"
curl -s -D - "$SERVER_URL/script.js" -o /dev/null

print_test "Image file"
curl -s -D - "$SERVER_URL/cutecat.png" -o /dev/null

print_test "HTML file with spaces"
curl -s -D - "$SERVER_URL/my%20file.txt" -o /dev/null

# ============================================================================
# 3. Check 404 for wrong URLs
# ============================================================================
print_header "3. Checking 404 Error Handling"
print_test "Non-existent HTML page"
curl -s -D - "$SERVER_URL/nonexistent-page.html" -o /dev/null

print_test "Non-existent directory"
curl -s -D - "$SERVER_URL/nonexistent-directory/" -o /dev/null

print_test "Non-existent asset"
curl -s -D - "$SERVER_URL/nonexistent.css" -o /dev/null

# ============================================================================
# 4. Check directory listings
# ============================================================================
print_header "4. Checking Directory Listings"
print_test "Directory with autoindex on"
curl -s -D - "$SERVER_URL/example.com/autoindex_on/" -o /dev/null

print_test "Directory with autoindex off"
curl -s -D - "$SERVER_URL/example.com/autoindex_off_no_index/" -o /dev/null

print_test "Directory with index file"
curl -s -D - "$SERVER_URL/example.com/" -o /dev/null

# ============================================================================
# 5. Check redirects
# ============================================================================
print_header "5. Checking Redirects"
print_test "Root path redirect (if configured)"
curl -s -D - -L "$SERVER_URL/" -o /dev/null

print_test "Directory without trailing slash"
curl -s -D - -L "$SERVER_URL/example.com" -o /dev/null

# ============================================================================
# 6. Check content types
# ============================================================================
print_header "6. Checking Content Types"
print_test "HTML content type"
curl -s -D - "$SERVER_URL/index.html" -o /dev/null | grep -i "content-type"

print_test "CSS content type"
curl -s -D - "$SERVER_URL/style.css" -o /dev/null | grep -i "content-type"

print_test "JS content type"
curl -s -D - "$SERVER_URL/script.js" -o /dev/null | grep -i "content-type"

print_test "PNG content type"
curl -s -D - "$SERVER_URL/cutecat.png" -o /dev/null | grep -i "content-type"

# ============================================================================
# 7. Edge cases
# ============================================================================
print_header "7. Testing Edge Cases"
print_test "Very long URL"
LONG_URL="$SERVER_URL/$(head -c 100 < /dev/zero | tr '\0' 'a').html"
curl -s -D - "$LONG_URL" -o /dev/null

print_test "URL with special characters"
curl -s -D - "$SERVER_URL/files/cat-css/cat.html" -o /dev/null

print_test "Empty path"
curl -s -D - "$SERVER_URL" -o /dev/null

print_test "Multiple slashes"
curl -s -D - "$SERVER_URL///" -o /dev/null

echo -e "\n=========================================="
echo -e "Static Site Tests Completed!"
echo -e "=========================================="
echo -e "\nManual Browser Testing:"
echo -e "1. Open $SERVER_URL in your browser"
echo -e "2. Check Network tab in dev tools"
echo -e "3. Verify all assets load correctly"
echo -e "4. Test navigation between pages"
echo -e "5. Check error pages look good" 