#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    cat <<'USAGE'
Usage: can_capture.sh [interface] [output_file]

Wraps candump from can-utils to capture Victron-compatible CAN traffic.

Arguments:
  interface    SocketCAN interface to sniff (default: can0)
  output_file  Optional path where the raw log will be mirrored.

Examples:
  sudo ./can_capture.sh           # Print to stdout from can0
  sudo ./can_capture.sh can1 log.txt

Press Ctrl+C to stop the capture. The script ensures timestamps are
preserved (-L flag) for later analysis of keepalive periods.
USAGE
    exit 0
fi

INTERFACE="${1:-can0}"
OUTPUT="${2:-}"

if ! command -v candump >/dev/null 2>&1; then
    echo "Error: candump (can-utils) is not installed or not in PATH" >&2
    exit 1
fi

echo "Capturing CAN traffic on ${INTERFACE}..." >&2
if [[ -n "${OUTPUT}" ]]; then
    candump -L "${INTERFACE}" | tee "${OUTPUT}"
else
    candump -L "${INTERFACE}"
fi
