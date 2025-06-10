#!/bin/bash
# filepath: tests/test_infinite_loop_protection.sh

HOST=localhost
PORT=8080
BASE_URL="http://$HOST:$PORT"

echo "=== Infinite Loop Detection Test ==="

# Function to test with timeout and detect loops
test_with_timeout() {
    local url="$1"
    local test_name="$2"
    local timeout_seconds=5
    
    echo "Testing: $test_name"
    echo "URL: $url"
    
    # Use timeout to prevent hanging
    local start_time=$(date +%s)
    local response=$(timeout $timeout_seconds curl -s -w "HTTPSTATUS:%{http_code}" "$url" 2>/dev/null)
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    if [ $? -eq 124 ]; then
        echo "❌ TIMEOUT after ${timeout_seconds}s - Possible infinite loop!"
        return 1
    elif [ $duration -ge $timeout_seconds ]; then
        echo "❌ SLOW RESPONSE (${duration}s) - Possible performance issue"
        return 1
    else
        local status=$(echo "$response" | grep -o "HTTPSTATUS:[0-9]*" | cut -d: -f2)
        echo "✓ RESPONSE in ${duration}s - Status: $status"
        return 0
    fi
}

# Test various endpoints that might cause loops
test_with_timeout "$BASE_URL/" "Root directory"
test_with_timeout "$BASE_URL/uploads/" "Uploads directory (with slash)"
test_with_timeout "$BASE_URL/uploads" "Uploads directory (without slash)"
test_with_timeout "$BASE_URL/api/" "API directory"
test_with_timeout "$BASE_URL/nonexistent/" "Non-existent directory"

echo "=== Loop Detection Complete ==="