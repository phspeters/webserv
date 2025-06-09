#!/bin/bash
# filepath: test_config_validation.sh

# Test script for webserv configuration validation
# This script creates various config files and tests validation

echo "=== WebServ Configuration Validation Test ==="
echo

# Create test directory structure
mkdir -p test_configs
mkdir -p test_www/valid
mkdir -p test_www/invalid
mkdir -p error_pages

# Create some test files
echo "<h1>Test Page</h1>" > test_www/valid/index.html
echo "404 Not Found" > error_pages/404.html
echo "500 Internal Server Error" > error_pages/500.html

# Test 1: Valid configuration
cat > test_configs/valid.conf << 'EOF'
server {
    listen 127.0.0.1:8080;
    server_name example.com www.example.com;
    client_max_body_size 10M;
    error_page 404 error_pages/404.html;
    error_page 500 error_pages/500.html;

    location / {
        root test_www/valid;
        index index.html;
        allow_methods GET POST;
        autoindex off;
        cgi off;
    }

    location /api {
        root test_www/valid;
        allow_methods GET POST DELETE;
        autoindex off;
        cgi on;
    }

    location /redirect-test {
        root test_www/valid;
        allow_methods GET;
        redirect 301 https://newdomain.com/new-path;
    }
}
EOF

# Test 2: Missing listen directive
cat > test_configs/no_listen.conf << 'EOF'
server {
    server_name example.com;
    
    location / {
        root test_www/valid;
        allow_methods GET;
    }
}
EOF

# Test 3: Invalid port number
cat > test_configs/invalid_port.conf << 'EOF'
server {
    listen 127.0.0.1:99999;
    server_name example.com;
    
    location / {
        root test_www/valid;
        allow_methods GET;
    }
}
EOF

# Test 4: Invalid IP address
cat > test_configs/invalid_ip.conf << 'EOF'
server {
    listen 300.300.300.300:8080;
    server_name example.com;
    
    location / {
        root test_www/valid;
        allow_methods GET;
    }
}
EOF

# Test 5: Missing location blocks
cat > test_configs/no_locations.conf << 'EOF'
server {
    listen 127.0.0.1:8080;
    server_name example.com;
}
EOF

# Test 6: Invalid location path
cat > test_configs/invalid_location_path.conf << 'EOF'
server {
    listen 127.0.0.1:8080;
    server_name example.com;
    
    location invalid-path {
        root test_www/valid;
        allow_methods GET;
    }
}
EOF

# Test 7: Location with invalid characters
cat > test_configs/invalid_path_chars.conf << 'EOF'
server {
    listen 127.0.0.1:8080;
    server_name example.com;
    
    location /path<>with"invalid*chars {
        root test_www/valid;
        allow_methods GET;
    }
}
EOF

# Test 8: Missing root directive
cat > test_configs/no_root.conf << 'EOF'
server {
    listen 127.0.0.1:8080;
    server_name example.com;
    
    location / {
        allow_methods GET;
        autoindex on;
    }
}
EOF

# Test 9: Non-existent root directory
cat > test_configs/invalid_root.conf << 'EOF'
server {
    listen 127.0.0.1:8080;
    server_name example.com;
    
    location / {
        root /non/existent/directory;
        allow_methods GET;
    }
}
EOF

# Test 10: Invalid HTTP methods
cat > test_configs/invalid_methods.conf << 'EOF'
server {
    listen 127.0.0.1:8080;
    server_name example.com;
    
    location / {
        root test_www/valid;
        allow_methods GET POST INVALID_METHOD;
    }
}
EOF

# Test 11: No allowed methods
cat > test_configs/no_methods.conf << 'EOF'
server {
    listen 127.0.0.1:8080;
    server_name example.com;
    
    location / {
        root test_www/valid;
        autoindex on;
    }
}
EOF

# Test 12: Invalid redirect status code
cat > test_configs/invalid_redirect_status.conf << 'EOF'
server {
    listen 127.0.0.1:8080;
    server_name example.com;
    
    location /redirect1 {
        redirect 200 /new-path;
    }
    
    location /redirect2 {
        redirect 404 /error-path;
    }
}
EOF

# Test 13: Invalid redirect URL format
cat > test_configs/invalid_redirect_url.conf << 'EOF'
server {
    listen 127.0.0.1:8080;
    server_name example.com;
    
    location /redirect1 {
        redirect 301 invalid-url;
    }
    
    location /redirect2 {
        redirect 302;
    }
    
    location /redirect3 {
        redirect 303 "";
    }
}
EOF

# Test 14: Invalid client_max_body_size
cat > test_configs/invalid_body_size.conf << 'EOF'
server {
    listen 127.0.0.1:8080;
    server_name example.com;
    client_max_body_size 0;
    
    location / {
        root test_www/valid;
        allow_methods GET;
    }
}

server {
    listen 127.0.0.1:8081;
    server_name example2.com;
    client_max_body_size 10X;
    
    location / {
        root test_www/valid;
        allow_methods GET;
    }
}

server {
    listen 127.0.0.1:8082;
    server_name example3.com;
    client_max_body_size -5M;
    
    location / {
        root test_www/valid;
        allow_methods GET;
    }
}
EOF

# Test 15: Valid redirect configurations
cat > test_configs/valid_redirects.conf << 'EOF'
server {
    listen 127.0.0.1:8080;
    server_name example.com;
    
    location /redirect-relative {
        root test_www/valid;
        allow_methods GET;
        redirect 301 /new-path;
    }
    
    location /redirect-absolute {
        root test_www/valid;
        allow_methods GET;
        redirect 302 https://newdomain.com/path;
    }
    
    location /redirect-http {
        root test_www/valid;
        allow_methods GET;
        redirect 307 http://oldsite.com/legacy;
    }
    
    location /redirect-complex {
        root test_www/valid;
        allow_methods GET;
        redirect 308 https://api.example.com/v2/endpoint;
    }
}
EOF

# Test 16: Complex valid configuration
cat > test_configs/complex_valid.conf << 'EOF'
server {
    listen 0.0.0.0:80;
    server_name example.com www.example.com;
    client_max_body_size 50M;
    error_page 404 error_pages/404.html;
    error_page 500 error_pages/500.html;

    location / {
        root test_www/valid;
        index index.html index.htm;
        allow_methods GET POST;
        autoindex off;
        cgi off;
    }

    location /uploads {
        root test_www/valid;
        allow_methods GET POST DELETE;
        autoindex on;
        cgi off;
    }

    location /api {
        root test_www/valid;
        allow_methods GET POST DELETE;
        autoindex off;
        cgi on;
    }

    location /old-section {
    root test_www/valid;
    allow_methods GET;
    redirect 301 /new-section;
    }
}

server {
    listen 127.0.0.1:8443;
    server_name secure.example.com;
    client_max_body_size 100M;

    location / {
        root test_www/valid;
        allow_methods GET POST;
        autoindex off;
        cgi off;
    }
}
EOF

# Function to run test
run_test() {
    local config_file="$1"
    local test_name="$2"
    local expected_result="$3"  # "PASS" or "FAIL"
    
    echo "Testing: $test_name"
    echo "Config: $config_file"
    
    if ../webserv "$config_file" --validate-only 2>/dev/null; then
        result="PASS"
    else
        result="FAIL"
    fi
    
    if [ "$result" = "$expected_result" ]; then
        echo "✅ EXPECTED $expected_result - GOT $result"
    else
        echo "❌ EXPECTED $expected_result - GOT $result"
    fi
    echo
    echo "---"
    echo
}

# Run all tests
echo "Running validation tests..."
echo

run_test "test_configs/valid.conf" "Valid configuration" "PASS"
run_test "test_configs/no_listen.conf" "Missing listen directive" "FAIL"
run_test "test_configs/invalid_port.conf" "Invalid port number (>65535)" "FAIL"
run_test "test_configs/invalid_ip.conf" "Invalid IP address" "FAIL"
run_test "test_configs/no_locations.conf" "Missing location blocks" "FAIL"
run_test "test_configs/invalid_location_path.conf" "Invalid location path (no /)" "FAIL"
run_test "test_configs/invalid_path_chars.conf" "Location path with invalid chars" "FAIL"
run_test "test_configs/no_root.conf" "Missing root directive" "FAIL"
run_test "test_configs/invalid_root.conf" "Non-existent root directory" "FAIL"
run_test "test_configs/invalid_methods.conf" "Invalid HTTP methods" "FAIL"
run_test "test_configs/no_methods.conf" "No allowed methods specified" "PASS"
run_test "test_configs/invalid_redirect_status.conf" "Invalid redirect status codes" "FAIL"
run_test "test_configs/invalid_redirect_url.conf" "Invalid redirect URL formats" "FAIL"
run_test "test_configs/invalid_body_size.conf" "Invalid client_max_body_size" "FAIL"
run_test "test_configs/valid_redirects.conf" "Valid redirect configurations" "PASS"
run_test "test_configs/complex_valid.conf" "Complex valid configuration" "PASS"

echo
echo "Test completed. Clean up test files:"
echo "rm -rf test_configs test_www error_pages"