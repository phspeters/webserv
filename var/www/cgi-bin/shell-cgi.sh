#!/bin/bash

# Test it by accessing the script via a web server configured to execute CGI scripts.
#http://localhost:8090/cgi-bin/shell-cgi.sh
#http://localhost:8090/cgi-bin/shell-cgi.sh?name=test&value=123

# Function to generate the HTML body
generate_html_body() {
    local post_data_content="$1"
    # Get current time
    local CURRENT_TIME=$(date "+%Y-%m-%d %H:%M:%S")

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
    <h1> Shell CGI Test Script</h1>
    
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
HTML_START

    # List important CGI environment variables
    local important_vars="SERVER_NAME SERVER_PORT REQUEST_METHOD REQUEST_URI QUERY_STRING CONTENT_TYPE CONTENT_LENGTH HTTP_HOST HTTP_USER_AGENT SCRIPT_FILENAME PATH_INFO"

    for var in $important_vars; do
        local value="${!var}"
        echo "                <p><strong>$var:</strong> "
        echo "                <span class=\"env-var\">${value:-Not Set}</span></p>"
    done

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
    echo "    <div class=\"section\">"
    echo "        <h2>POST Data</h2>"

     # Handle POST data using the passed argument
    if [ "$REQUEST_METHOD" = "POST" ]; then
        if [ -n "$CONTENT_LENGTH" ] && [ "$CONTENT_LENGTH" -gt 0 ]; then
            echo "        <p><strong>POST Data Read:</strong> <pre class=\"env-var\">$post_data_content</pre></p>"
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

    <p><small>CGI script executed successfully!</small></p>
</body>
</html>
HTML_END
}

POST_DATA_CAPTURED=""
if [ "$REQUEST_METHOD" = "POST" ]; then
    if [ -n "$CONTENT_LENGTH" ] && [ "$CONTENT_LENGTH" -gt 0 ]; then
        read -r -n "$CONTENT_LENGTH" POST_DATA_CAPTURED
    fi
fi

# Generate the HTML body and store it in a variable, passing the captured POST data.
html_body=$(generate_html_body "$POST_DATA_CAPTURED")

# Calculate the content length
content_length=$(echo -n "$html_body" | wc -c)

# Output HTTP headers
printf "Content-Type: text/html\n"
printf "Content-Length: %d\n" "$content_length"
printf "\n"

# Output the HTML body
printf "%s" "$html_body"