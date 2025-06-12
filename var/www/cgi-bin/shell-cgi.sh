#!/bin/bash

# Test it by accessing the script via a web server configured to execute CGI scripts.
#http://localhost:8090/cgi-bin/shell-cgi.sh
#http://localhost:8090/cgi-bin/shell-cgi.sh?name=test&value=123

# Output HTTP headers
echo "Content-Type: text/html"
echo "" # Empty line separates headers from body

# Get current time
CURRENT_TIME=$(date "+%Y-%m-%d %H:%M:%S")

# Start HTML output
cat << HTML_START
<!DOCTYPE html>
<html>
<head>
    <title>Shell CGI Test</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        .section { margin: 20px 0; padding: 10px; border-left: 3px solid #e76f51; }
        .env-var { background: #f5f5f5; padding: 2px 4px; font-family: monospace; }
        table { border-collapse: collapse; width: 100%; }
        th, td { text-align: left; padding: 8px; border-bottom: 1px solid #ddd; }
        th { background-color: #f2f2f2; }
    </style>
</head>
<body>
    <h1>🐚 Shell CGI Test Script</h1>
    
    <div class="section">
        <h2>Basic Info</h2>
        <p><strong>Current Time:</strong> $CURRENT_TIME</p>
        <p><strong>Shell Version:</strong> $BASH_VERSION</p>
        <p><strong>Script Path:</strong> $(readlink -f "$0")</p>
    </div>
    
    <div class="section">
        <h2>Request Method</h2>
        <p><strong>Method:</strong> <span class="env-var">$REQUEST_METHOD</span></p>
    </div>

    <div class="section">
        <h2>Key Environment Variables</h2>
        <table>
            <tr>
                <th>Variable</th>
                <th>Value</th>
            </tr>
HTML_START

# List important CGI environment variables
important_vars="SERVER_NAME SERVER_PORT REQUEST_METHOD REQUEST_URI QUERY_STRING CONTENT_TYPE CONTENT_LENGTH HTTP_HOST HTTP_USER_AGENT SCRIPT_NAME PATH_INFO"

for var in $important_vars; do
    value="${!var}"
    echo "            <tr>"
    echo "                <td><strong>$var</strong></td>"
    echo "                <td><span class=\"env-var\">${value:-Not Set}</span></td>"
    echo "            </tr>"
done

echo "        </table>"
echo "    </div>"

echo "    <div class=\"section\">"
echo "        <h2>Query String Parameters</h2>"

# Parse and display query string
if [ -n "$QUERY_STRING" ]; then
    echo "        <p><strong>Raw Query String:</strong> <span class=\"env-var\">$QUERY_STRING</span></p>"
    echo "        <ul>"
    
    # Split query string by '&'
    IFS="&" read -ra PARAMS <<< "$QUERY_STRING"
    for param in "${PARAMS[@]}"; do
        # Check if parameter has a value
        if [[ "$param" == *"="* ]]; then
            key="${param%%=*}"
            value="${param#*=}"
            echo "            <li><strong>$key:</strong> $value</li>"
        else
            echo "            <li>$param (no value)</li>"
        fi
    done
    
    echo "        </ul>"
else
    echo "        <p><em>No query string parameters</em></p>"
fi

echo "    </div>"
echo ""
echo "    <div class=\"section\">"
echo "        <h2>POST Data</h2>"

# Handle POST data
if [ "$REQUEST_METHOD" = "POST" ]; then
    if [ -n "$CONTENT_LENGTH" ]; then
        # Read POST data
        post_data=$(dd bs="$CONTENT_LENGTH" count=1 2>/dev/null)
        echo "        <p><strong>POST Data:</strong> <span class=\"env-var\">$post_data</span></p>"
    else
        echo "        <p><em>POST request but no content length or data</em></p>"
    fi
else
    echo "        <p><em>Not a POST request</em></p>"
fi

# Finish HTML
cat << HTML_END
    </div>

    <div class="section">
        <h2>Test Links</h2>
        <p>Test different scenarios:</p>
        <ul>
            <li><a href="?name=test&value=123">With query parameters</a></li>
            <li><a href="?hello=world&foo=bar&empty">Multiple parameters</a></li>
        </ul>
    </div>

    <p><small>✅ CGI script executed successfully!</small></p>
</body>
</html>
HTML_END