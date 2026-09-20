#!/usr/bin/env bash
# Install blenus overlay into a micro:bit μT-Kernel tree and patch the host.
#
# Expected order:
#   1. Expand 362_mbit_mtk3.zip (mtkernel_3)
#   2. Clone this repository as a sibling
#   3. ./patch/apply.sh --mtk3 /path/to/mtkernel_3
#
# Copies (into --mtk3):
#   app_sample/          replaces PMC sample
#   device/blenus/       driver
#   device/include/dev_blenus.h
#   components/          SoftDevice / nRF5 SDK / BLE
#   build_make/blenus_overlay.mk and build_make/mtkernel_3/.../subdir.mk
#   etc/linker/microbit/tkernel_ble_wsd.ld
#
# Then patches (idempotent):
#   dispatch.S     PendSV: FPCA clear; SoftDevice tasks on PSP (IRQs on MSP)
#   cpu_task.h     SoftDevice: EXC_RETURN 0xFFFFFFFD (Thread / PSP)
#   reset_hdl.c    ASPEN/LSPEN off if SoftDevice; skip VTOR rewrite;
#                  use __data_start__ / _bss_start (RAM) for copy/zero
#   interrupt.c    knl_fpca_clear at HLL / SysTick entry
#   config.h       CNF_EXC_STACK_SIZE 4096; CNF_TMP_STACK_SIZE 2048
#   makefile       -include blenus_overlay.mk; link EXTOBJS; build .hex
#   config_device.h / device.h / dev_def.h / devinit.c
#                  DEVCNF_USE_BLENUS and knl_start_device registration
#   vector_tbl.c / sysdepend.h
#                  SWI2_EGU2_IRQHandler for SoftDevice BLE events
#   sysdef.h       SysTick priority 6 (must not preempt SoftDevice SWI2)
#
# Author: koh aiaida (koh@aiaida.jp)
# Copyright (c) 2025-2026 koh aiaida
#
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

PATCH_DIR="$(cd "$(dirname "$0")" && pwd)"
MTK3=""
DRY_RUN=0

usage() {
  cat <<'EOF'
Usage:
  apply.sh --mtk3 <μT-Kernel-root> [--dry-run]

  --mtk3     Path to micro:bit μT-Kernel (mtkernel_3), or its parent
  --dry-run  List copy and patch steps only
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mtk3) MTK3="$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "$MTK3" ]]; then
  echo "--mtk3 is required" >&2
  usage >&2
  exit 2
fi

resolve_python() {
  local cand
  for cand in python python3 py; do
    if command -v "$cand" >/dev/null 2>&1; then
      if "$cand" -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" 2>/dev/null; then
        echo "$cand"
        return 0
      fi
    fi
  done
  return 1
}

PY="$(resolve_python)" || {
  echo "Python 3.8+ is required to install the overlay" >&2
  exit 1
}

args=(--mtk3 "$MTK3")
if [[ "$DRY_RUN" -eq 1 ]]; then
  args+=(--dry-run)
fi

exec "$PY" "$PATCH_DIR/apply.py" "${args[@]}"
