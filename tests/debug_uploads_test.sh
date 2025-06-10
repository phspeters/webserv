#!/bin/bash
# filepath: tests/debug_uploads_test.sh

HOST=localhost
PORT=8080
BASE_URL="http://$HOST:$PORT"

echo "=== Debug Uploads Directory Test ==="

# First, check if uploads directory exists
echo "1. Checking directory structure..."
ls -la tests/test_www/valid/
echo

# Check if uploads directory exists
if [ -d "tests/test_www/valid/uploads" ]; then
    echo "✓ uploads directory exists"
    ls -la tests/test_www/valid/uploads/
else
    echo "✗ uploads directory does not exist - creating it..."
    mkdir -p tests/test_www/valid/uploads
    echo "Test file" > tests/test_www/valid/uploads/test.txt
    echo "✓ Created uploads directory with test file"
fi
echo

# Test the problematic request with timeout
echo "2. Testing GET /uploads/ with 5 second timeout..."
timeout 5s curl -v "$BASE_URL/uploads/" 2>&1 | head -20
echo

echo "3. Testing GET /uploads (without trailing slash)..."
timeout 5s curl -v "$BASE_URL/uploads" 2>&1 | head -10
echo

echo "4. Testing if server is still responsive..."
timeout 3s curl -s "$BASE_URL/" > /dev/null
if [ $? -eq 0 ]; then
    echo "✓ Server is still responsive"
else
    echo "✗ Server appears to be stuck or unresponsive"
fi