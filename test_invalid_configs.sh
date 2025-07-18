#!/bin/bash

# List of invalid config files
INVALID_CONFS=(
  content_outside_server.conf
  invalid_allowed_methods.conf
  invalid_autoindex.conf
  invalid_error_page_code.conf
  invalid_listen_port.conf
  invalid_location_path.conf
  invalid_root_path.conf
  missing_listen.conf
  missing_location.conf
  missing_root_in_location.conf
  missing_semicolon.conf
  missing_server_brace.conf
  nested_server.conf
  port_out_of_range.conf
  unbalanced_braces.conf
)

PASS=0
FAIL=0

for conf in "${INVALID_CONFS[@]}"; do
  echo "Testing $conf..."
  ./webserv config/invalid_conf/$conf > /dev/null 2>&1
  if [ $? -ne 0 ]; then
    echo "PASS: $conf (server failed to start as expected)"
    PASS=$((PASS+1))
  else
    echo "FAIL: $conf (server started, but should have failed)"
    FAIL=$((FAIL+1))
  fi
  echo
done

echo "Summary: $PASS passed, $FAIL failed."
exit $FAIL 