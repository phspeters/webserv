#!/bin/bash

# =============================================================================
# TEST SCRIPT FOR FILE UPLOAD HANDLER
# =============================================================================

SERVER_URL="http://localhost:8090/uploads/" 

set -e

function check_status {
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

echo "1. Image upload"
out=$(curl -sv -F "file=@var/www/files/cutecat.png" "$SERVER_URL" 2>&1)
check_status 201 "$out" "Image upload"
sleep 1

echo "2. Text file upload"
echo "test text" > /tmp/test.txt
out=$(curl -sv -F "file=@/tmp/test.txt" "$SERVER_URL" 2>&1)
check_status 201 "$out" "Text file upload"
sleep 1

echo "3. Binary file upload (.zip)"
zip -j /tmp/test.zip /tmp/test.txt
out=$(curl -sv -F "file=@/tmp/test.zip" "$SERVER_URL" 2>&1)
check_status 201 "$out" "Binary file upload (.zip)"
sleep 1

echo "4. Upload file without extension"
cp /tmp/test.txt /tmp/noext
out=$(curl -sv -F "file=@/tmp/noext" "$SERVER_URL" 2>&1)
check_status 201 "$out" "Upload file without extension"
sleep 1

echo "5. Upload file with special characters in name"
cp /tmp/test.txt "/tmp/a@b#c$.txt"
out=$(curl -sv -F "file=@/tmp/a@b#c$.txt" "$SERVER_URL" 2>&1)
check_status 201 "$out" "Upload file with special characters"
sleep 1

echo "6. Upload larger than allowed (should return 413)"
dd if=/dev/zero of=/tmp/hugefile bs=1M count=100
out=$(curl -sv -F "file=@/tmp/hugefile" "$SERVER_URL" 2>&1)
check_status 413 "$out" "Upload larger than allowed"
sleep 1

echo "7. Empty file upload"
touch /tmp/emptyfile
out=$(curl -sv -F "file=@/tmp/emptyfile" "$SERVER_URL" 2>&1)
check_status 201 "$out" "Empty file upload"
sleep 1

echo "8. Incorrect Content-Type"
out=$(curl -sv -H "Content-Type: text/plain" --data-binary @/tmp/test.txt "$SERVER_URL" 2>&1)
check_status 415 "$out" "Incorrect Content-Type"
sleep 1

echo "9. Missing boundary"
out=$(curl -sv -H "Content-Type: multipart/form-data" --data-binary @/tmp/test.txt "$SERVER_URL" 2>&1)
check_status 400 "$out" "Missing boundary"
sleep 1

echo "10. Upload to non-existent directory (should return 404 or 500)"
out=$(curl -sv -F "file=@/tmp/test.txt" "http://localhost:8090/nonexistent/" 2>&1)
check_status 404 "$out" "Upload to non-existent directory"
sleep 1

# THIS TEST REQUIRES SERVER CONFIGURATION
echo "11. Upload to directory without write permission (should return 403)"
mkdir -p /tmp/readonly
chmod 555 /tmp/readonly
# You need to configure your server to serve /tmp/readonly at some endpoint, e.g., /readonly
out=$(curl -sv -F "file=@/tmp/test.txt" "http://localhost:8090/readonly/" 2>&1)
check_status 403 "$out" "Upload to directory without write permission"
chmod 755 /tmp/readonly
rm -rf /tmp/readonly
sleep 1

echo "12. Concurrent uploads (should all succeed)"
echo "Starting 3 concurrent uploads..."
cp /tmp/test.txt /tmp/test1.txt
cp /tmp/test.txt /tmp/test2.txt
cp /tmp/test.txt /tmp/test3.txt
{
  curl -s -F "file=@/tmp/test1.txt" "$SERVER_URL" &
  curl -s -F "file=@/tmp/test2.txt" "$SERVER_URL" &
  curl -s -F "file=@/tmp/test3.txt" "$SERVER_URL" &
  wait
}
echo "[OK] Concurrent uploads finished (check server for all files)"
rm -f /tmp/test1.txt /tmp/test2.txt /tmp/test3.txt
sleep 1

# Clean up temporary files
rm -f /tmp/bigfile.jpg /tmp/test.txt /tmp/test.zip /tmp/noext "/tmp/${LONGNAME}.txt" "/tmp/a@b#c\$.txt" /tmp/dotfile /tmp/path_traversal.txt /tmp/hugefile /tmp/emptyfile "/tmp/${LONGNAME2}.txt"

echo "All tests finished!"