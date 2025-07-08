#!/usr/bin/env python3

import os
import sys
from datetime import datetime

# Generate all the dynamic content first
current_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
python_version = sys.version
script_path = os.path.abspath(__file__)
request_method = os.environ.get('REQUEST_METHOD', 'Not Set')

# Generate environment variables section
env_vars_html = ""
important_vars = [
    'SERVER_NAME', 'SERVER_PORT', 'REQUEST_METHOD', 'REQUEST_URI', 
    'QUERY_STRING', 'CONTENT_TYPE', 'CONTENT_LENGTH', 'HTTP_HOST',
    'HTTP_USER_AGENT', 'SCRIPT_NAME', 'PATH_INFO'
]

for var in important_vars:
    value = os.environ.get(var, 'Not Set')
    env_vars_html += f'        <p><strong>{var}:</strong> <span class="env-var">{value}</span></p>\r\n'

# Generate query string section
query_string_html = ""
query_string = os.environ.get('QUERY_STRING', '')
if query_string:
    query_string_html += f'        <p><strong>Raw Query String:</strong> <span class="env-var">{query_string}</span></p>\r\n'
    # Simple query string parsing (for basic testing)
    params = query_string.split('&')
    query_string_html += '        <ul>\r\n'
    for param in params:
        if '=' in param:
            key, value = param.split('=', 1)
            query_string_html += f'            <li><strong>{key}:</strong> {value}</li>\r\n'
        else:
            query_string_html += f'            <li>{param} (no value)</li>\r\n'
    query_string_html += '        </ul>\r\n'
else:
    query_string_html += '        <p><em>No query string parameters</em></p>\r\n'

# Generate POST data section
post_data_html = ""
if os.environ.get('REQUEST_METHOD') == 'POST':
    content_length = os.environ.get('CONTENT_LENGTH')
    if content_length and content_length.isdigit():
        post_data = sys.stdin.read(int(content_length))
        post_data_html += f'        <p><strong>POST Data:</strong> <span class="env-var">{post_data}</span></p>\r\n'
    else:
        post_data_html += '        <p><em>POST request but no content length or data</em></p>\r\n'
else:
    post_data_html += '        <p><em>Not a POST request</em></p>\r\n'

# Build the complete HTML content
html_content = f"""<!DOCTYPE html>
<html>
<head>
    <title>Python CGI Test</title>
    <style>
        body {{ font-family: Arial, sans-serif; margin: 40px; }}
        .section {{ margin: 20px 0; padding: 10px; border-left: 3px solid #007acc; }}
        .env-var {{ background: #f5f5f5; padding: 2px 4px; font-family: monospace; }}
    </style>
</head>
<body>
    <h1>Python CGI Test Script</h1>
    
    <div class="section">
        <h2>Basic Info</h2>
        <p><strong>Current Time:</strong> {current_time}</p>
        <p><strong>Python Version:</strong> {python_version}</p>
        <p><strong>Script Path:</strong> {script_path}</p>
    </div>
    
    <div class="section">
        <h2>Request Method</h2>
        <p><strong>Method:</strong> <span class="env-var">{request_method}</span></p>
    </div>

    <div class="section">
        <h2>Key Environment Variables</h2>
{env_vars_html}    </div>
    
    <div class="section">
        <h2>Query String Parameters</h2>
{query_string_html}    </div>

    <div class="section">
        <h2>POST Data</h2>
{post_data_html}    </div>

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
</html>"""

# Calculate content length
content_bytes = html_content.encode('utf-8')
content_length = len(content_bytes)

# Build complete response (headers + body) as bytes
response = f"Content-Type: text/html\r\nContent-Length: {content_length}\r\n\r\n".encode('utf-8') + content_bytes

# Send everything at once using binary mode
sys.stdout.buffer.write(response)
sys.stdout.buffer.flush()