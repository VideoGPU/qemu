#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
run_neorv32_twd_socket.sh
=========================

Uses exact default paths for qemu-system-riscv32 and bootloader.

Starts QEMU with:
  -chardev socket,id=twdm,path=/tmp/twd-i2c.sock,server=on,wait=off
  -device i2c-master-chardev,chardev=twdm,bus=i2c

Supports overrides via env vars:
  QEMU_BIN=... BIOS=... TWD_SOCKET=... scripts/run_neorv32_twd_socket.sh

Quick run:
  scripts/run_neorv32_twd_socket.sh

In another shell:
  python3 scripts/neorv32_twd_client.py --socket /tmp/twd-i2c.sock

Extra usage notes:
  - Show this help:
      scripts/run_neorv32_twd_socket.sh --help
  - Override socket path:
      TWD_SOCKET=/tmp/my-twd.sock scripts/run_neorv32_twd_socket.sh
  - Override BIOS path:
      BIOS=/path/to/neorv32_raw_exe.bin scripts/run_neorv32_twd_socket.sh
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

QEMU_BIN_DEFAULT="/home/smishash/shonot/sources/qemu/build/qemu-system-riscv32"
BIOS_DEFAULT="/mnt/shonot/fpga_projects/neorv32/sw/bootloader/neorv32_raw_exe.bin"
SOCKET_DEFAULT="/tmp/twd-i2c.sock"

QEMU_BIN="${QEMU_BIN:-$QEMU_BIN_DEFAULT}"
BIOS="${BIOS:-$BIOS_DEFAULT}"
TWD_SOCKET="${TWD_SOCKET:-$SOCKET_DEFAULT}"

if [[ ! -x "$QEMU_BIN" ]]; then
  echo "ERROR: QEMU binary not executable: $QEMU_BIN" >&2
  exit 1
fi

if [[ ! -f "$BIOS" ]]; then
  echo "ERROR: BIOS file not found: $BIOS" >&2
  exit 1
fi

rm -f "$TWD_SOCKET"

echo "Launching NEORV32 with TWD host socket..."
echo "  QEMU_BIN=$QEMU_BIN"
echo "  BIOS=$BIOS"
echo "  TWD_SOCKET=$TWD_SOCKET"
echo

echo "After QEMU starts, in another shell run:"
echo "  python3 scripts/neorv32_twd_client.py --socket $TWD_SOCKET"
echo

exec "$QEMU_BIN" \
  -nographic \
  -machine neorv32 \
  -bios "$BIOS" \
  -chardev "socket,id=twdm,path=$TWD_SOCKET,server=on,wait=off" \
  -device i2c-master-chardev,chardev=twdm,bus=i2c
