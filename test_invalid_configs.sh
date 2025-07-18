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

TMP_ERR=tmp_webserv_stderr.log

echo "=== Testing INVALID configs (should FAIL) ==="
for conf in "${INVALID_CONFS[@]}"; do
  echo "Testing $conf..."
  ./webserv config/invalid_conf/$conf > /dev/null 2> "$TMP_ERR"
  if grep -q '\[FATAL\]\|\[ERROR\]' "$TMP_ERR"; then
    echo "PASS: $conf (fatal/error detected as expected)"
    echo "---- Fatal/Error Output ----"
    grep '\[FATAL\]\|\[ERROR\]' "$TMP_ERR"
    echo "----------------------------"
    PASS=$((PASS+1))
  else
    echo "FAIL: $conf (no fatal/error detected, should have failed)"
    echo "---- Output ----"
    cat "$TMP_ERR"
    echo "----------------"
    FAIL=$((FAIL+1))
  fi
  echo
done

rm -f "$TMP_ERR"

echo "Summary: $PASS passed, $FAIL failed."
exit $FAIL 