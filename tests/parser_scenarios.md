
### I. Global Configuration File Structure & Syntax

These tests check the overall integrity and syntax of the `.conf` file itself.

*   **Valid Files:**
    *   A file with a single, valid `server` block.
    *   A file with multiple, valid `server` blocks.
    *   A completely empty file (should be valid, but result in no servers).
    *   A file containing only comments (`# ...`).
    *   A file containing only whitespace (spaces, tabs, newlines).
    *   A file with comments and whitespace interspersed correctly.
*   **Structural Errors:**
    *   Content outside of any `server` block (e.g., `listen 8080;` at the top level).
    *   A `server` block that is missing its opening brace `{`.
    *   A `server` block that is missing its closing brace `}`.
    *   Unbalanced braces (e.g., `server { location / { }`).
    *   Nested `server` blocks (e.g., `server { server { ... } }`), which should be invalid.
*   **Syntax Errors:**
    *   A directive missing its required semicolon (e.g., `listen 8080`).
    *   A directive with a value but no key (e.g., `8080;`).
    *   A directive with a key but no value (e.g., `listen;`).
    *   A directive with garbage characters before the key (e.g., `!@# listen 8080;`).
    *   A directive with garbage characters after the semicolon (e.g., `listen 8080; !@#`).

### II. `server` Block Directives

These tests focus on the directives allowed directly inside a `server` block.

*   **`listen`**
    *   **Valid Formats:**
        *   Port only: `listen 80;`
        *   Host and port: `listen 127.0.0.1:8080;`
        *   Hostname and port: `listen localhost:8000;`
        *   `0.0.0.0` to listen on all interfaces: `listen 0.0.0.0:80;`
    *   **Edge Cases (Values):**
        *   Lowest valid port: `listen 1;`
        *   Highest valid port: `listen 65535;`
        *   Port `0` (should be invalid).
        *   Port `65536` (should be invalid).
        *   Negative port number: `listen -1;`
    *   **Invalid Formats:**
        *   Non-numeric port: `listen badport;`
        *   Missing port: `listen 127.0.0.1:;`
        *   Missing host: `listen :8080;`
        *   Extra data: `listen 8080 extra;`
        *   Invalid IP address format: `listen 999.999.999.999:80;`
*   **`server_name`**
    *   **Valid Formats:**
        *   Single name: `server_name example.com;`
        *   Multiple names: `server_name example.com www.example.com;`
        *   Wildcard names (if supported): `server_name *.example.com;`
    *   **Edge Cases:**
        *   Extremely long server name.
        *   Server name with numbers and hyphens: `server_name my-1st-site.com;`
        *   Duplicate names in the same directive: `server_name a.com a.com;`
*   **`error_page`**
    *   **Valid Formats:**
        *   Single code: `error_page 404 /404.html;`
        *   Multiple codes: `error_page 500 502 503 /50x.html;`
    *   **Edge Cases (Values):**
        *   Lowest valid error code: `error_page 300 /error.html;`
        *   Highest valid error code: `error_page 599 /error.html;`
        *   Path with subdirectories: `error_page 404 /errors/pages/404.html;`
    *   **Invalid Formats:**
        *   Code outside the 300-599 range: `error_page 200 /ok.html;`
        *   Non-numeric code: `error_page 40x /error.html;`
        *   Path is not absolute: `error_page 404 error.html;`
        *   No path provided: `error_page 404;`
        *   No codes provided: `error_page /error.html;`
*   **`client_max_body_size`**
    *   **Valid Formats:**
        *   Bytes (no unit): `client_max_body_size 1024;`
        *   Kilobytes: `client_max_body_size 10k;` or `10K;`
        *   Megabytes: `client_max_body_size 1m;` or `1M;`
    *   **Edge Cases:**
        *   Size `0` (should probably be invalid or mean "no limit").
        *   Very large number.
        *   Whitespace between number and unit: `client_max_body_size 10 M;` (should be invalid).
    *   **Invalid Formats:**
        *   Negative value: `client_max_body_size -10M;`
        *   Invalid unit: `client_max_body_size 10X;`
        *   Non-numeric value: `client_max_body_size tenM;`

### III. `location` Block & Its Directives

These tests focus on the `location` block and its specific context.

*   **Location Path**
    *   **Valid Formats:**
        *   Root location: `location / { ... }`
        *   Sub-path: `location /images/ { ... }`
        *   CGI extension (if supported): `location .py { ... }`
    *   **Invalid Formats:**
        *   Path not starting with `/` or `.`: `location images/ { ... }`
        *   Path with invalid characters: `location /a*b/ { ... }`
        *   Empty path: `location "" { ... }`
*   **Location-Specific Directives**
    *   **`root`**:
        *   Valid: `root /var/www/html;`
        *   Invalid: `root relative/path;` (should require an absolute path).
        *   Edge Case: `root /;`
    *   **`index`**:
        *   Valid: `index index.html;`, `index index.php index.html;`
        *   Invalid: `index /path/to/index.html;` (should not be a path).
    *   **`autoindex`**:
        *   Valid: `autoindex on;`, `autoindex off;`
        *   Invalid: `autoindex yes;`, `autoindex 1;`
    *   **`allowed_methods`** (or similar):
        *   Valid: `allowed_methods GET POST;`
        *   Invalid: `allowed_methods JUMP;` (unsupported method).
        *   Edge Case: Empty list (should default to `GET` or be an error).
    *   **`redirect`** (or `return`):
        *   Valid: `return 301 /new-url;`, `return 302 http://example.com;`
        *   Invalid: `return 200 /ok;` (not a redirect code), `return 301;` (missing URL).

### IV. Overall Validation Logic

After parsing, the server configuration as a whole must be valid.

*   **Server-Level Validation:**
    *   A server block must have a `listen` directive.
    *   A server block must have at least one `location` block.
    *   A server block must have a `location / { ... }` block to act as a fallback.
    *   Two `server` blocks cannot listen on the exact same host:port combination.
    *   If two `server` blocks listen on the same port but have different `server_name`s, this is valid.
*   **Location-Level Validation:**
    *   A location block must have a `root` or `alias` directive (unless it's purely for proxying/CGI).
    *   The path specified in `root` must exist and be a directory.
    *   The path specified in `root` must have read permissions.