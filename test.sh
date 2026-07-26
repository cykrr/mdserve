#!/bin/bash
# test.sh — regression tests for mdserve security fixes.
# Starts a local mdserve, runs attack payloads, checks responses, kills server.
set -euo pipefail

PORT="${TEST_PORT:-18099}"
BASE="http://127.0.0.1:${PORT}"
PASSED=0
FAILED=0
SERVER_PID=""

cleanup() {
  [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null || true
  wait "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT

start_server() {
  ./mdserve "http://127.0.0.1:${PORT}" ./md &
  SERVER_PID=$!
  sleep 0.5
  # Wait until listening
  for i in $(seq 1 20); do
    curl -s -o /dev/null "$BASE/" 2>/dev/null && return 0
    sleep 0.1
  done
  echo "FATAL: server didn't start"
  exit 1
}

assert_status() {
  local desc="$1" url="$2" expected="$3"
  shift 3
  local actual
  actual=$(curl -s -o /dev/null -w "%{http_code}" "$@" "$url" 2>/dev/null || echo "000")
  if [ "$actual" = "$expected" ]; then
    echo "  PASS: $desc ($expected)"
    PASSED=$((PASSED + 1))
  else
    echo "  FAIL: $desc — expected $expected, got $actual"
    FAILED=$((FAILED + 1))
  fi
}

assert_header() {
  local desc="$1" url="$2" header="$3" expected="$4"
  local actual
  actual=$(curl -sI "$url" 2>/dev/null | grep -i "^${header}:" | tr -d '\r' || echo "")
  if echo "$actual" | grep -q "$expected"; then
    echo "  PASS: $desc"
    PASSED=$((PASSED + 1))
  else
    echo "  FAIL: $desc — expected '$expected' in '$actual'"
    FAILED=$((FAILED + 1))
  fi
}

echo "=== mdserve regression tests ==="
start_server
echo ""

echo "--- path traversal ---"
# %2e%2e%2f decodes to "../" — should be blocked
assert_status "%2e%2e%2f (.. encoded)" \
  "${BASE}/%2e%2e%2fetc%2fpasswd" "403"
# --path-as-is prevents curl from normalizing ../ in the URL path
assert_status "%2e%2e/ (.. literal)" \
  "${BASE}/../etc/passwd" "403" --path-as-is
# Double-encoding: %252e decodes to %2e, which is a literal percent sign
# in the filesystem — not a traversal. Expect 404.
assert_status "double-encoded %252e%252e%252f (404, not traversal)" \
  "${BASE}/%252e%252e%252fetc%252fpasswd" "404"

echo ""
echo "--- IPv4 SSRF ---"
assert_status "127.0.0.1" \
  "${BASE}/remote/http%3A%2F%2F127.0.0.1%2Ftest.md" "403"
assert_status "10.0.0.1 (private)" \
  "${BASE}/remote/http%3A%2F%2F10.0.0.1%2Ftest.md" "403"
assert_status "192.168.1.1 (private)" \
  "${BASE}/remote/http%3A%2F%2F192.168.1.1%2Ftest.md" "403"
assert_status "169.254.169.254 (link-local)" \
  "${BASE}/remote/http%3A%2F%2F169.254.169.254%2Ftest.md" "403"

echo ""
echo "--- IPv6 SSRF ---"
assert_status "[::1] (loopback)" \
  "${BASE}/remote/http%3A%2F%2F%5B%3A%3A1%5D%2Ftest.md" "403"

echo ""
echo "--- scheme / path restrictions ---"
assert_status "ftp:// blocked" \
  "${BASE}/remote/ftp%3A%2F%2Fexample.com%2Ftest.md" "400"
assert_status "file:// blocked" \
  "${BASE}/remote/file%3A%2F%2F%2Fetc%2Fpasswd" "400"
assert_status "no .md extension" \
  "${BASE}/remote/https%3A%2F%2Fexample.com%2F" "400"
assert_status "empty path" \
  "${BASE}/remote/https%3A%2F%2Fexample.com" "400"

echo ""
echo "--- X-Content-Type-Options ---"
assert_header "nosniff on HTML response" \
  "${BASE}/" "X-Content-Type-Options" "nosniff"
assert_header "nosniff on 404" \
  "${BASE}/nonexistent" "X-Content-Type-Options" "nosniff"

echo ""
echo "--- oversized remote body ---"
# Create a fake HTTP server that returns a huge body, then test
# (Skip for now — needs a local test server. Placeholder.)
echo "  SKIP: oversized body (needs test HTTP server)"

echo ""
echo "--- symlink traversal ---"
# Clean up any previous symlink
rm -f ./md/_test_symlink.md
ln -sf /etc/passwd ./md/_test_symlink.md 2>/dev/null || true
assert_status "symlink to /etc/passwd" \
  "${BASE}/_test_symlink.md" "404"
rm -f ./md/_test_symlink.md

echo ""
echo "--- XSS in filename (directory listing) ---"
# Create a file with XSS payload in name
touch "./md/\"><script>alert(1)</script>.md" 2>/dev/null || true
listing=$(curl -s "${BASE}/" 2>/dev/null || echo "")
if echo "$listing" | grep -q "<script>alert(1)</script>"; then
  echo "  FAIL: XSS payload in filename rendered unescaped"
  FAILED=$((FAILED + 1))
else
  echo "  PASS: XSS payload in filename escaped"
  PASSED=$((PASSED + 1))
fi
rm -f "./md/\"><script>alert(1)</script>.md"

echo ""
echo "========================================"
echo "Results: ${PASSED} passed, ${FAILED} failed"
[ "$FAILED" -eq 0 ] && echo "All tests passed!" || echo "Some tests FAILED!"
exit "$FAILED"
