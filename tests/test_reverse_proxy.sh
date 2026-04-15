#!/bin/bash
# tests/test_reverse_proxy.sh
# Manual test script for reverse proxy
# Requires: upstream server running on port 19090, proxy server running on port 19091
#
# Quick start:
#   1. Build: cmake --build build -j$(nproc)
#   2. Run integration test binary (starts both servers):
#      ./build/bin/test_reverse_proxy_integration --gtest_also_run_disabled_tests
#   3. In a separate terminal, run this script.

set -e

PROXY_HOST="${PROXY_HOST:-localhost}"
PROXY_PORT="${PROXY_PORT:-8080}"
BASE_URL="http://${PROXY_HOST}:${PROXY_PORT}"

PASS=0
FAIL=0

check() {
    local description="$1"
    local expected="$2"
    local actual="$3"
    if [ "$actual" = "$expected" ]; then
        echo "[PASS] $description"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $description"
        echo "       expected: $expected"
        echo "       actual:   $actual"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== Reverse Proxy Manual Tests ==="
echo "Target: ${BASE_URL}"
echo ""

echo "--- Test 1: HTTP proxy (should forward to upstream) ---"
echo "curl -s ${BASE_URL}/api/health -H 'Host: app.mintcocoa.cc'"
STATUS=$(curl -s -o /dev/null -w '%{http_code}' \
    "${BASE_URL}/api/health" \
    -H 'Host: app.mintcocoa.cc' || echo "000")
check "Proxy /api/health returns 200" "200" "$STATUS"
echo ""

echo "--- Test 2: Static file serving (non-proxy path) ---"
echo "curl -s ${BASE_URL}/ -H 'Host: blog.mintcocoa.cc'"
STATUS=$(curl -s -o /dev/null -w '%{http_code}' \
    "${BASE_URL}/" \
    -H 'Host: blog.mintcocoa.cc' || echo "000")
echo "  HTTP status: $STATUS (200 if static files configured, 404 otherwise)"
echo ""

echo "--- Test 3: Health check endpoint ---"
echo "curl -s ${BASE_URL}/health"
BODY=$(curl -s "${BASE_URL}/health" || echo "")
echo "  Response body: $BODY"
echo ""

echo "--- Test 4: Unknown route (should 404) ---"
echo "curl -s -o /dev/null -w '%{http_code}' ${BASE_URL}/unknown-path-xyz"
STATUS=$(curl -s -o /dev/null -w '%{http_code}' \
    "${BASE_URL}/unknown-path-xyz" || echo "000")
check "Unknown route returns 404" "404" "$STATUS"
echo ""

echo "--- Test 5: Proxy strips path prefix ---"
echo "curl -s ${BASE_URL}/api/documents -H 'Host: app.mintcocoa.cc'"
BODY=$(curl -s \
    "${BASE_URL}/api/documents" \
    -H 'Host: app.mintcocoa.cc' || echo "")
echo "  Response body: $BODY"
echo ""

echo "--- Test 6: POST forwarded to upstream ---"
echo "curl -s -X POST ${BASE_URL}/api/items -H 'Host: app.mintcocoa.cc' -d '{}'"
STATUS=$(curl -s -o /dev/null -w '%{http_code}' \
    -X POST "${BASE_URL}/api/items" \
    -H 'Host: app.mintcocoa.cc' \
    -H 'Content-Type: application/json' \
    -d '{}' || echo "000")
echo "  HTTP status: $STATUS"
echo ""

echo "==================================="
echo "Results: ${PASS} passed, ${FAIL} failed"
echo "==================================="

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
