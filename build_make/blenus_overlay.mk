################################################################################
# blenus overlay (new file; does not replace PMC makefile / microbit.mk)
#
# In the host build_make/makefile, after `include microbit.mk`, add:
#   -include blenus_overlay.mk
#
# Author: koh aiaida (koh@aiaida.jp)
# Copyright (c) 2025-2026 koh aiaida
#
# SPDX-License-Identifier: Apache-2.0
################################################################################

INCLUDE_SOFTDEVICE ?= 1
OBJCOPY ?= arm-none-eabi-objcopy

# Included before `all:` in PMC makefile; keep the default goal on the ELF.
.DEFAULT_GOAL := all

# Force ELF entry to Reset_Handler (INSERT AFTER in the BLE linker script
# would otherwise leave ENTRY(_start) at .text).
LFLAGS += -Wl,-e,Reset_Handler

-include mtkernel_3/device/blenus/subdir.mk

ifeq ($(INCLUDE_SOFTDEVICE), 1)
    CFLAGS += -DSOFTDEVICE_PRESENT -DS113 -DNRF_SD_BLE_API_VERSION=7
    CFLAGS += -DINCLUDE_SOFTDEVICE
    ASFLAGS += -DSOFTDEVICE_PRESENT -DINCLUDE_SOFTDEVICE
    CFLAGS += -mgeneral-regs-only
    EXTOBJS := ../components/softdevice/mbr.o ../components/softdevice/softdevice.o
    LNKFILE := "../etc/linker/microbit/tkernel_ble_wsd.ld"
    include mtkernel_3/components/subdir.mk
endif

# The ISR / SoftDevice path stays on the hard-float ABI but must not emit VFP instructions.
CFLAGS_NOFPU := -mgeneral-regs-only
mtkernel_3/components/softdevice/%.o: CFLAGS += $(CFLAGS_NOFPU)
mtkernel_3/components/ble/%.o: CFLAGS += $(CFLAGS_NOFPU)
mtkernel_3/device/blenus/sysdepend/nrf5/%.o: CFLAGS += $(CFLAGS_NOFPU)
