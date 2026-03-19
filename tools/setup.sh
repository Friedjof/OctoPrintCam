#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DATA_TEMPLATE="${REPO_ROOT}/data-template"
DATA_DIR="${REPO_ROOT}/data"
PIO_HOME="${REPO_ROOT}/.pio-home"

info() { printf "[INFO] %s\n" "$*"; }
warn() { printf "[WARN] %s\n" "$*"; }
error() { printf "[ERROR] %s\n" "$*" >&2; }

info "Running repository setup from ${REPO_ROOT}"

# Check for PlatformIO CLI
if ! command -v pio >/dev/null 2>&1; then
  error "PlatformIO CLI (pio) not found. Install instructions: https://docs.platformio.org/en/latest/core/installation.html"
  exit 1
fi

# Setup local PlatformIO home
mkdir -p "${PIO_HOME}"
info "Ensured local PlatformIO home at ${PIO_HOME}"

# Setup data directory from template
if [ ! -d "${DATA_TEMPLATE}" ]; then
  warn "${DATA_TEMPLATE} is missing; skipping data directory setup."
else
  if [ -d "${DATA_DIR}" ]; then
    info "${DATA_DIR} already exists; skipping copy."
  else
    cp -R "${DATA_TEMPLATE}" "${DATA_DIR}"
    info "Created ${DATA_DIR} from template. Review config.json if needed."
  fi
fi

# Prefetch PlatformIO packages
info "Attempting to prefetch PlatformIO packages (may require network)…"
if PLATFORMIO_HOME_DIR="${PIO_HOME}" pio pkg update --only-platform --only-library >/dev/null 2>&1; then
  info "PlatformIO packages updated."
else
  warn "Could not update PlatformIO packages. This is safe to ignore if offline; rerun after gaining network access."
fi

info "Setup complete. Next steps:"
info "  1. Edit platformio.ini to configure your target environment"
info "  2. Edit data/config.json if using configuration files"
info "  3. Run 'pio run' to build the firmware"
info "  4. Run 'pio run --target upload' to flash the device"
