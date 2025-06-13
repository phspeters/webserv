#!/usr/bin/env python3

# Test it by accessing the script via a web browser or curl command:
# http://localhost:8090/cgi-bin/python-cgi.py
# http://localhost:8090/cgi-bin/python-cgi.py?name=test&value=123

import os
import sys
from datetime import datetime

# CGI scripts must output headers first
print("Content-Type: text/html", end="\r\n")
print("", end="\r\n")  # Empty line required to separate headers from body

# HTML response body
print("""<!DOCTYPE html>
<html>
<head>
    <title>Python CGI Test</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        .section { margin: 20px 0; padding: 10px; border-left: 3px solid #007acc; }
        .env-var { background: #f5f5f5; padding: 2px 4px; font-family: monospace; }
    </style>
</head>
<body>
    <h1>Python CGI Test Script</h1>
    
    <div class="section">
        <h2>Basic Info</h2>
        <p><strong>Current Time:</strong> """ + datetime.now().strftime("%Y-%m-%d %H:%M:%S") + """</p>
        <p><strong>Python Version:</strong> """ + sys.version + """</p>
        <p><strong>Script Path:</strong> """ + os.path.abspath(__file__) + """</p>
    </div>
    
    <div class="section">
        <h2>Request Method</h2>
        <p><strong>Method:</strong> <span class="env-var">""" + os.environ.get('REQUEST_METHOD', 'Not Set') + """</span></p>
    </div>

    <div class="section">
        <h2>Key Environment Variables</h2>""", end="\r\n")

# Display important CGI environment variables
important_vars = [
    'SERVER_NAME', 'SERVER_PORT', 'REQUEST_METHOD', 'REQUEST_URI', 
    'QUERY_STRING', 'CONTENT_TYPE', 'CONTENT_LENGTH', 'HTTP_HOST',
    'HTTP_USER_AGENT', 'SCRIPT_NAME', 'PATH_INFO'
]

for var in important_vars:
    value = os.environ.get(var, 'Not Set')
    print(f'        <p><strong>{var}:</strong> <span class="env-var">{value}</span></p>', end="\r\n")

print("""    </div>
    
    <div class="section">
        <h2>Query String Parameters</h2>""", end="\r\n")

query_string = os.environ.get('QUERY_STRING', '')
if query_string:
    print(f'        <p><strong>Raw Query String:</strong> <span class="env-var">{query_string}</span></p>', end="\r\n")
    # Simple query string parsing (for basic testing)
    params = query_string.split('&')
    print('        <ul>', end="\r\n")
    for param in params:
        if '=' in param:
            key, value = param.split('=', 1)
            print(f'            <li><strong>{key}:</strong> {value}</li>', end="\r\n")
        else:
            print(f'            <li>{param} (no value)</li>', end="\r\n")
    print('        </ul>')
else:
    print('        <p><em>No query string parameters</em></p>', end="\r\n")

print("""    </div>

    <div class="section">
        <h2>POST Data</h2>""", end="\r\n")

if os.environ.get('REQUEST_METHOD') == 'POST':
    content_length = os.environ.get('CONTENT_LENGTH')
    if content_length and content_length.isdigit():
        post_data = sys.stdin.read(int(content_length))
        print(f'        <p><strong>POST Data:</strong> <span class="env-var">{post_data}</span></p>', end="\r\n")
    else:
        print('        <p><em>POST request but no content length or data</em></p>', end="\r\n")
else:
    print('        <p><em>Not a POST request</em></p>', end="\r\n")

print("""    </div>

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
</html>""", end="\r\n")