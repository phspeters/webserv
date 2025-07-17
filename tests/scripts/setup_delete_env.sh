#!/bin/bash

# =============================================================================
# WEBSERV: DELETE HANDLER TEST ENVIRONMENT SETUP SCRIPT
# =============================================================================
# Description:
# This script creates the necessary directory and file structure for running
# automated tests for the FileDeleteHandler.
# =============================================================================

# The root directory for all delete test files
TEST_ROOT="tests/test_www/delete"

echo "Setting up DELETE test environment in '${TEST_ROOT}'..."

# --- Clean up previous environment ---
if [ -d "$TEST_ROOT" ]; then
    echo "Removing existing test directory..."
    # Restore permissions to allow deletion
    chmod 755 "$TEST_ROOT/no_parent_write_perms" 2>/dev/null
    rm -rf "$TEST_ROOT"
fi

# --- Create directory structure ---
echo "Creating directory structure..."
mkdir -p "$TEST_ROOT/a_directory"
mkdir -p "$TEST_ROOT/no_parent_write_perms"

# --- Create test files ---
echo "Creating test files..."
echo "This file is meant to be deleted." > "$TEST_ROOT/deletable_file.txt"
echo "This file is read-only, but its parent directory is writable." > "$TEST_ROOT/protected_file.txt"
echo "This file should be unreachable for deletion." > "$TEST_ROOT/no_parent_write_perms/unreachable.txt"

# --- Set specific permissions for tests ---
echo "Setting special file permissions..."

# Test 2.3: File is read-only, but parent directory is writable.
# The OS should allow deletion.
chmod 444 "$TEST_ROOT/protected_file.txt"
echo "Set read-only permissions on 'protected_file.txt'"

# Test 2.4: Parent directory has no write permissions.
# The OS should prevent deletion of any file inside.
chmod 555 "$TEST_ROOT/no_parent_write_perms"
echo "Set no-write permissions on 'no_parent_write_perms/' directory"

echo ""
echo "DELETE test environment setup complete."
echo "Directory structure created at: $(pwd)/$TEST_ROOT"