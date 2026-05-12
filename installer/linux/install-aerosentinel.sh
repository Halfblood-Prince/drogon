#!/usr/bin/env bash
set -euo pipefail

APP_NAME="AeroSentinel Control Center"
APP_ID="aerosentinel-control-center"
APP_BIN="aerosentinel-control"
DEFAULT_PORT="8080"
DEFAULT_BIND_ADDRESS="127.0.0.1"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
MODE="auto"
INSTALL_DIR=""
INSTALL_SERVICE="0"
NO_LAUNCH="0"
PORT="${PORT:-$DEFAULT_PORT}"
BIND_ADDRESS="${AEROSENTINEL_BIND_ADDRESS:-$DEFAULT_BIND_ADDRESS}"

usage() {
  cat <<EOF
Usage: ./install-aerosentinel.sh [options]

Options:
  --user                 Install for the current user.
  --system               Install system-wide. Requires root or sudo.
  --prefix PATH          Install to a custom directory.
  --service              Install and enable a systemd service.
  --port PORT            Default launcher/service port. Default: ${DEFAULT_PORT}
  --bind ADDRESS         Default bind address. Default: ${DEFAULT_BIND_ADDRESS}
  --no-launch            Do not start the app after installation.
  -h, --help             Show this help.

Examples:
  ./install-aerosentinel.sh --user
  sudo ./install-aerosentinel.sh --system --service --port 8080
EOF
}

fail() {
  echo "error: $*" >&2
  exit 1
}

info() {
  echo "==> $*"
}

is_root() {
  [[ "${EUID}" -eq 0 ]]
}

detect_arch() {
  case "$(uname -m)" in
    x86_64|amd64)
      echo "x64"
      ;;
    aarch64|arm64)
      echo "arm64"
      ;;
    *)
      fail "unsupported Linux architecture: $(uname -m). AeroSentinel ships Linux builds for x64 and arm64."
      ;;
  esac
}

resolve_mode() {
  if [[ "${MODE}" == "auto" ]]; then
    if is_root; then
      MODE="system"
    else
      MODE="user"
    fi
  fi

  if [[ "${MODE}" == "system" && ! is_root ]]; then
    fail "--system requires root. Run with sudo or use --user."
  fi
}

validate_install_dir() {
  case "${INSTALL_DIR}" in
    ""|"/"|"/usr"|"/usr/"|"/usr/local"|"/usr/local/"|"/opt"|"/opt/"|"${HOME}"|"${HOME}/")
      fail "refusing unsafe install directory: ${INSTALL_DIR}"
      ;;
  esac
}

install_file_tree() {
  local source="$1"
  local destination="$2"

  mkdir -p "${destination}"
  cp -a "${source}/." "${destination}/"
  chmod +x "${destination}/${APP_BIN}"
}

create_launcher() {
  local launcher_path="$1"
  local install_dir="$2"

  mkdir -p "$(dirname -- "${launcher_path}")"
  cat >"${launcher_path}" <<EOF
#!/usr/bin/env bash
set -euo pipefail

export PORT="\${PORT:-${PORT}}"
export AEROSENTINEL_BIND_ADDRESS="\${AEROSENTINEL_BIND_ADDRESS:-${BIND_ADDRESS}}"

if [[ -f "\${AEROSENTINEL_ENV_FILE:-}" ]]; then
  set -a
  source "\${AEROSENTINEL_ENV_FILE}"
  set +a
elif [[ -f "\$HOME/.config/aerosentinel/aerosentinel.env" ]]; then
  set -a
  source "\$HOME/.config/aerosentinel/aerosentinel.env"
  set +a
elif [[ -f "/etc/aerosentinel/aerosentinel.env" ]]; then
  set -a
  source "/etc/aerosentinel/aerosentinel.env"
  set +a
fi

cd "${install_dir}"
exec "${install_dir}/${APP_BIN}" "\$@"
EOF
  chmod +x "${launcher_path}"
}

create_env_file() {
  local env_file="$1"

  if [[ -f "${env_file}" ]]; then
    return
  fi

  mkdir -p "$(dirname -- "${env_file}")"
  cat >"${env_file}" <<EOF
# AeroSentinel runtime settings
# Change these before exposing the app beyond localhost.
AEROSENTINEL_USER=operator
AEROSENTINEL_PASSWORD=change-this-password
AEROSENTINEL_BIND_ADDRESS=${BIND_ADDRESS}
PORT=${PORT}
EOF
  chmod 600 "${env_file}" || true
}

create_systemd_service() {
  local service_path="$1"
  local launcher_path="$2"
  local env_file="$3"
  local user_service="$4"
  local wanted_by="default.target"

  if [[ "${user_service}" != "1" ]]; then
    wanted_by="multi-user.target"
  fi

  mkdir -p "$(dirname -- "${service_path}")"
  cat >"${service_path}" <<EOF
[Unit]
Description=${APP_NAME}
After=network-online.target

[Service]
Type=simple
EnvironmentFile=${env_file}
ExecStart=${launcher_path}
Restart=on-failure
RestartSec=3

[Install]
WantedBy=${wanted_by}
EOF

  if [[ "${user_service}" == "1" ]]; then
    systemctl --user daemon-reload
    systemctl --user enable --now "${APP_ID}.service"
  else
    systemctl daemon-reload
    systemctl enable --now "${APP_ID}.service"
  fi
}

create_desktop_entry() {
  local desktop_path="$1"
  local launcher_path="$2"

  mkdir -p "$(dirname -- "${desktop_path}")"
  cat >"${desktop_path}" <<EOF
[Desktop Entry]
Type=Application
Name=${APP_NAME}
Comment=Open the AeroSentinel local control center
Exec=sh -c '${launcher_path} >/tmp/aerosentinel-control-center.log 2>&1 & sleep 1; xdg-open http://127.0.0.1:${PORT}/mission/alpha-0426'
Terminal=false
Categories=Network;Utility;
EOF
}

create_uninstaller() {
  local uninstall_path="$1"
  local install_dir="$2"
  local launcher_path="$3"
  local env_file="$4"
  local desktop_path="$5"
  local service_path="$6"
  local user_service="$7"

  cat >"${uninstall_path}" <<EOF
#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${service_path}" && -f "${service_path}" ]]; then
  if [[ "${user_service}" == "1" ]]; then
    systemctl --user disable --now "${APP_ID}.service" 2>/dev/null || true
    rm -f "${service_path}"
    systemctl --user daemon-reload 2>/dev/null || true
  else
    systemctl disable --now "${APP_ID}.service" 2>/dev/null || true
    rm -f "${service_path}"
    systemctl daemon-reload 2>/dev/null || true
  fi
fi

rm -f "${launcher_path}"
rm -f "${desktop_path}"
rm -rf "${install_dir}"

echo "${APP_NAME} removed."
echo "Credentials file was left in place: ${env_file}"
EOF
  chmod +x "${uninstall_path}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --user)
      MODE="user"
      ;;
    --system)
      MODE="system"
      ;;
    --prefix)
      shift
      [[ $# -gt 0 ]] || fail "--prefix requires a path"
      INSTALL_DIR="$1"
      ;;
    --service)
      INSTALL_SERVICE="1"
      ;;
    --port)
      shift
      [[ $# -gt 0 ]] || fail "--port requires a value"
      PORT="$1"
      ;;
    --bind)
      shift
      [[ $# -gt 0 ]] || fail "--bind requires a value"
      BIND_ADDRESS="$1"
      ;;
    --no-launch)
      NO_LAUNCH="1"
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      fail "unknown option: $1"
      ;;
  esac
  shift
done

resolve_mode

ARCH="$(detect_arch)"
PAYLOAD_DIR="${SCRIPT_DIR}/payload/${ARCH}"
[[ -d "${PAYLOAD_DIR}" ]] || fail "installer payload not found: ${PAYLOAD_DIR}"

if [[ -z "${INSTALL_DIR}" ]]; then
  if [[ "${MODE}" == "system" ]]; then
    INSTALL_DIR="/opt/${APP_ID}"
  else
    INSTALL_DIR="${HOME}/.local/share/${APP_ID}"
  fi
fi
validate_install_dir

if [[ "${MODE}" == "system" ]]; then
  LAUNCHER_PATH="/usr/local/bin/aerosentinel-control-center"
  ENV_FILE="/etc/aerosentinel/aerosentinel.env"
  DESKTOP_PATH="/usr/local/share/applications/${APP_ID}.desktop"
  SERVICE_PATH="/etc/systemd/system/${APP_ID}.service"
  USER_SERVICE="0"
else
  LAUNCHER_PATH="${HOME}/.local/bin/aerosentinel-control-center"
  ENV_FILE="${HOME}/.config/aerosentinel/aerosentinel.env"
  DESKTOP_PATH="${HOME}/.local/share/applications/${APP_ID}.desktop"
  SERVICE_PATH="${HOME}/.config/systemd/user/${APP_ID}.service"
  USER_SERVICE="1"
fi

info "Installing ${APP_NAME} (${ARCH}) to ${INSTALL_DIR}"
install_file_tree "${PAYLOAD_DIR}" "${INSTALL_DIR}"
create_env_file "${ENV_FILE}"
create_launcher "${LAUNCHER_PATH}" "${INSTALL_DIR}"
create_desktop_entry "${DESKTOP_PATH}" "${LAUNCHER_PATH}"
create_uninstaller "${INSTALL_DIR}/uninstall-aerosentinel.sh" "${INSTALL_DIR}" "${LAUNCHER_PATH}" "${ENV_FILE}" "${DESKTOP_PATH}" "${SERVICE_PATH}" "${USER_SERVICE}"

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$(dirname -- "${DESKTOP_PATH}")" >/dev/null 2>&1 || true
fi

if [[ "${INSTALL_SERVICE}" == "1" ]]; then
  command -v systemctl >/dev/null 2>&1 || fail "--service requires systemd/systemctl"
  create_systemd_service "${SERVICE_PATH}" "${LAUNCHER_PATH}" "${ENV_FILE}" "${USER_SERVICE}"
fi

info "Installed ${APP_NAME}."
echo "Launcher: ${LAUNCHER_PATH}"
echo "Config:   ${ENV_FILE}"
echo "URL:      http://127.0.0.1:${PORT}/mission/alpha-0426"
echo "Remove:   ${INSTALL_DIR}/uninstall-aerosentinel.sh"

if [[ "${NO_LAUNCH}" != "1" && "${INSTALL_SERVICE}" != "1" ]]; then
  info "Starting ${APP_NAME}. Press Ctrl+C to stop."
  exec "${LAUNCHER_PATH}"
fi
