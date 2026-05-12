#!/usr/bin/env bash
set -euo pipefail

APP_PATH="${1:-build/aerosentinel-control}"
BASE_URL="${AEROSENTINEL_SMOKE_BASE_URL:-http://127.0.0.1:18080}"
COOKIE_JAR="$(mktemp)"
BODY_FILE="$(mktemp)"
HEADER_FILE="$(mktemp)"
SERVER_LOG="$(mktemp)"
SERVER_PID=""

cleanup() {
  if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
    kill "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
  fi

  rm -f "${COOKIE_JAR}" "${BODY_FILE}" "${HEADER_FILE}" "${SERVER_LOG}"
}
trap cleanup EXIT

assert_contains() {
  local file="$1"
  local expected="$2"
  if ! grep -q "${expected}" "${file}"; then
    echo "Expected '${expected}' in ${file}" >&2
    echo "--- ${file} ---" >&2
    cat "${file}" >&2 || true
    exit 1
  fi
}

assert_status() {
  local actual="$1"
  local expected="$2"
  local label="$3"
  if [[ "${actual}" != "${expected}" ]]; then
    echo "${label}: expected HTTP ${expected}, got ${actual}" >&2
    echo "--- headers ---" >&2
    cat "${HEADER_FILE}" >&2 || true
    echo "--- body ---" >&2
    cat "${BODY_FILE}" >&2 || true
    echo "--- server log ---" >&2
    cat "${SERVER_LOG}" >&2 || true
    exit 1
  fi
}

if [[ ! -x "${APP_PATH}" ]]; then
  echo "App executable not found or not executable: ${APP_PATH}" >&2
  exit 1
fi

export PORT=18080
export AEROSENTINEL_BIND_ADDRESS=127.0.0.1
export AEROSENTINEL_USER=operator
export AEROSENTINEL_PASSWORD=smoke-password

"${APP_PATH}" >"${SERVER_LOG}" 2>&1 &
SERVER_PID="$!"

ready=0
for _ in {1..100}; do
  if curl --silent --fail "${BASE_URL}/login" >/dev/null 2>&1; then
    ready=1
    break
  fi

  if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    echo "Server exited before becoming ready." >&2
    cat "${SERVER_LOG}" >&2 || true
    exit 1
  fi

  sleep 0.1
done

if [[ "${ready}" != "1" ]]; then
  echo "Server did not become ready at ${BASE_URL}." >&2
  cat "${SERVER_LOG}" >&2 || true
  exit 1
fi

status="$(curl --silent --show-error --output "${BODY_FILE}" --dump-header "${HEADER_FILE}" --write-out "%{http_code}" "${BASE_URL}/login")"
assert_status "${status}" "200" "login page"
assert_contains "${BODY_FILE}" "Operator Sign In"

status="$(curl --silent --show-error --output "${BODY_FILE}" --dump-header "${HEADER_FILE}" --write-out "%{http_code}" "${BASE_URL}/mission/alpha-0426")"
assert_status "${status}" "302" "protected dashboard redirect"
assert_contains "${HEADER_FILE}" "Location: /login"

status="$(curl --silent --show-error --output "${BODY_FILE}" --dump-header "${HEADER_FILE}" --write-out "%{http_code}" "${BASE_URL}/api/mission/alpha-0426")"
assert_status "${status}" "401" "protected API rejection"
assert_contains "${BODY_FILE}" "authentication_required"

status="$(curl --silent --show-error --output "${BODY_FILE}" --dump-header "${HEADER_FILE}" --write-out "%{http_code}" \
  --data "username=operator&password=wrong-password" \
  "${BASE_URL}/login")"
assert_status "${status}" "303" "bad login redirect"
assert_contains "${HEADER_FILE}" "Location: /login?error=1"

status="$(curl --silent --show-error --output "${BODY_FILE}" --dump-header "${HEADER_FILE}" --write-out "%{http_code}" \
  --cookie-jar "${COOKIE_JAR}" \
  --data "username=operator&password=smoke-password" \
  "${BASE_URL}/login")"
assert_status "${status}" "303" "good login redirect"
assert_contains "${HEADER_FILE}" "Set-Cookie: aerosentinel_session="
assert_contains "${HEADER_FILE}" "Location: /mission/alpha-0426"

status="$(curl --silent --show-error --location --output "${BODY_FILE}" --dump-header "${HEADER_FILE}" --write-out "%{http_code}" \
  --cookie-jar "${COOKIE_JAR}" \
  --cookie "${COOKIE_JAR}" \
  --data "username=operator&password=smoke-password" \
  "${BASE_URL}/login")"
assert_status "${status}" "200" "good login followed redirect"
assert_contains "${BODY_FILE}" "AeroSentinel"
assert_contains "${BODY_FILE}" "Mission: ALPHA-0426"

status="$(curl --silent --show-error --output "${BODY_FILE}" --dump-header "${HEADER_FILE}" --write-out "%{http_code}" \
  --cookie "${COOKIE_JAR}" \
  "${BASE_URL}/mission/alpha-0426")"
assert_status "${status}" "200" "authenticated dashboard"
assert_contains "${BODY_FILE}" "AeroSentinel"

status="$(curl --silent --show-error --output "${BODY_FILE}" --dump-header "${HEADER_FILE}" --write-out "%{http_code}" \
  --cookie "${COOKIE_JAR}" \
  "${BASE_URL}/api/mission/alpha-0426")"
assert_status "${status}" "200" "authenticated API"
assert_contains "${BODY_FILE}" "ALPHA-0426"
assert_contains "${BODY_FILE}" "Sentinel-7B"

status="$(curl --silent --show-error --output "${BODY_FILE}" --dump-header "${HEADER_FILE}" --write-out "%{http_code}" \
  --cookie "${COOKIE_JAR}" \
  "${BASE_URL}/logout")"
assert_status "${status}" "302" "logout redirect"
assert_contains "${HEADER_FILE}" "Location: /login"

status="$(curl --silent --show-error --output "${BODY_FILE}" --dump-header "${HEADER_FILE}" --write-out "%{http_code}" \
  --cookie "${COOKIE_JAR}" \
  "${BASE_URL}/api/mission/alpha-0426")"
assert_status "${status}" "401" "API rejection after logout"

echo "AeroSentinel smoke tests passed."
