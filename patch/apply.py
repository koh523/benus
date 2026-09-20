#!/usr/bin/env python3
"""Install blenus overlay into a micro:bit μT-Kernel tree and patch the host.

Copies device/blenus, components, app_sample, overlay makefiles, and the BLE
linker script into --mtk3, then applies host-tree patches. Each patched
host file also gets a short "modified for benus" note placed right after
PMC's own header comment, so the file is identifiable as patched even in
isolation from this repository.

Idempotent: copies overwrite; already-patched files are skipped.

Usage:
  python patch/apply.py --mtk3 /path/to/mtkernel_3

Author: koh aiaida (koh@aiaida.jp)
Copyright (c) 2025-2026 koh aiaida

SPDX-License-Identifier: Apache-2.0
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path


class PatchError(Exception):
    pass


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


# (relative path, replace destination directory)
COPY_TREES: list[tuple[str, bool]] = [
    ("app_sample", True),
    ("device/blenus", False),
    ("components", False),
    ("build_make/mtkernel_3", False),
]

COPY_FILES: list[str] = [
    "device/include/dev_blenus.h",
    "build_make/blenus_overlay.mk",
    "etc/linker/microbit/tkernel_ble_wsd.ld",
]

IGNORE = shutil.ignore_patterns(".git", ".DS_Store")


def read_text(path: Path) -> tuple[str, str]:
    data = path.read_bytes()
    nl = "\r\n" if b"\r\n" in data else "\n"
    text = data.decode("utf-8")
    if nl == "\r\n":
        text = text.replace("\r\n", "\n")
    return text, nl


def write_text(path: Path, text: str, nl: str) -> None:
    if nl == "\r\n":
        text = text.replace("\r\n", "\n").replace("\n", "\r\n")
    path.write_bytes(text.encode("utf-8"))


def require(path: Path) -> None:
    if not path.is_file():
        raise PatchError(f"missing {path}")


def already(text: str, needle: str) -> bool:
    return needle in text


def must_contain(path: Path, text: str, needle: str) -> None:
    if needle not in text:
        raise PatchError(f"marker not found in {path}: {needle!r}")


def replace_once(path: Path, old: str, new: str, skip_if: str | None = None) -> str:
    text, nl = read_text(path)
    if skip_if and already(text, skip_if):
        return "skip"
    must_contain(path, text, old)
    if text.count(old) != 1:
        raise PatchError(f"expected 1 occurrence in {path}, found {text.count(old)}")
    write_text(path, text.replace(old, new, 1), nl)
    return "ok"


def insert_after(path: Path, marker: str, insert: str, skip_if: str) -> str:
    text, nl = read_text(path)
    if already(text, skip_if):
        return "skip"
    must_contain(path, text, marker)
    write_text(path, text.replace(marker, marker + insert, 1), nl)
    return "ok"


def is_mtk3_tree(path: Path) -> bool:
    return all(
        (path / need).is_file()
        for need in (
            "config/config.h",
            "device/include/device.h",
            "build_make/makefile",
        )
    )


def find_mtk3(path: Path) -> Path:
    if is_mtk3_tree(path):
        return path
    nested = path / "mtkernel_3"
    if is_mtk3_tree(nested):
        return nested
    return path


def _is_relative_to(path: Path, other: Path) -> bool:
    try:
        path.relative_to(other)
        return True
    except ValueError:
        return False


def copy_tree(src: Path, dst: Path, replace: bool) -> str:
    if not src.is_dir():
        raise PatchError(f"missing overlay {src}")
    src_r, dst_r = src.resolve(), dst.resolve()
    if src_r == dst_r:
        return "skip"
    if _is_relative_to(dst_r, src_r) or _is_relative_to(src_r, dst_r):
        raise PatchError(f"refusing to copy overlapping paths: {src} -> {dst}")
    if replace and dst.exists():
        shutil.rmtree(dst)
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(src, dst, dirs_exist_ok=True, ignore=IGNORE)
    return "ok"


def copy_file(src: Path, dst: Path) -> str:
    if not src.is_file():
        raise PatchError(f"missing overlay {src}")
    if src.resolve() == dst.resolve():
        return "skip"
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    return "ok"


def install_overlay(src_root: Path, mtk3: Path) -> list[tuple[str, str]]:
    results: list[tuple[str, str]] = []
    for rel, replace in COPY_TREES:
        results.append((rel, copy_tree(src_root / rel, mtk3 / rel, replace)))
    for rel in COPY_FILES:
        results.append((rel, copy_file(src_root / rel, mtk3 / rel)))
    return results


def _replace_one_of(text: str, path: Path, olds: list[str], new: str, skip_if: str) -> tuple[str, bool]:
    if skip_if in text:
        return text, False
    found = next((old for old in olds if old in text), None)
    if found is None:
        raise PatchError(f"site not found in {path}: {skip_if!r}")
    if text.count(found) != 1:
        raise PatchError(f"expected 1 occurrence in {path}, found {text.count(found)}")
    return text.replace(found, new, 1), True


def patch_dispatch(mtk3: Path) -> str:
    """PendSV: FPCA clear, and SoftDevice tasks on PSP (IRQs stay on MSP)."""
    path = mtk3 / "kernel/sysdepend/cpu/core/armv7m/dispatch.S"
    require(path)
    text, nl = read_text(path)
    orig = text

    text, _ = _replace_one_of(
        text,
        path,
        [
            "Csym(knl_dispatch_entry):\t\n"
            "/*----------------- Start dispatch processing. -----------------*/\n"
        ],
        "Csym(knl_dispatch_entry):\t\n"
        "/*----------------- Start dispatch processing. -----------------*/\n"
        "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
        "\tmrs\tr0, control\n"
        "\tbic\tr0, r0, #4\t\t\t// CONTROL.FPCA (SD IRQ needs basic frame)\n"
        "\tmsr\tcontrol, r0\n"
        "\tisb\n"
        "#endif\n",
        "SD IRQ needs basic frame",
    )

    text, _ = _replace_one_of(
        text,
        path,
        [
            "\tldr\tsp, =(Csym(knl_tmp_stack) + TMP_STACK_SIZE)\t// Set temporal stack\n"
            "\tb\tl_dispatch_100\n"
        ],
        "#if !(defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT)\n"
        "\tldr\tsp, =(Csym(knl_tmp_stack) + TMP_STACK_SIZE)\t// Set temporal stack\n"
        "#endif\n"
        "\tb\tl_dispatch_100\n",
        "#if !(defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT)",
    )

    text, _ = _replace_one_of(
        text,
        path,
        [
            "l_dispatch_000: \n"
            "\tpush\t{r4-r11}\n"
            "\tpush\t{lr}\n",
            "l_dispatch_000:\n"
            "\tpush\t{r4-r11}\n"
            "\tpush\t{lr}\n",
        ],
        "l_dispatch_000:\n"
        "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
        "\tmrs\tr2, psp\n"
        "\tstmdb\tr2!, {r4-r11}\n"
        "\tstmdb\tr2!, {lr}\n"
        "#else\n"
        "\tpush\t{r4-r11}\n"
        "\tpush\t{lr}\n"
        "#endif\n",
        "mrs\tr2, psp",
    )

    text, _ = _replace_one_of(
        text,
        path,
        [
            "#if USE_FPU\t\t\t// Save FPU register\n"
            "\tldr\tr2, [r1, #TCB_tskatr]\n"
            "\tands\tr2, r2, #TA_FPU\n"
            "\tbeq\tl_dispatch_010\t\t\t// ctxtsk is not a TA_FPU attribute.\n"
            "\n"
            "\tands\tr3,lr, #EXPRN_NO_FPU\n"
            "\tbne\tl_dispatch_010\t\t\t// ctxtsk does not execute FPU instructions.\n"
            "\n"
            "\tvpush\t{s16-s31}\t\t\t// Push FPU register (S15-S31)\n"
            "\tpush\t{r3}\t\t\t\t//FPU usage flag\n"
        ],
        "#if USE_FPU\t\t\t// Save FPU register\n"
        "\tldr\tr3, [r1, #TCB_tskatr]\n"
        "\tands\tr3, r3, #TA_FPU\n"
        "\tbeq\tl_dispatch_010\t\t\t// ctxtsk is not a TA_FPU attribute.\n"
        "\n"
        "\tands\tr3,lr, #EXPRN_NO_FPU\n"
        "\tbne\tl_dispatch_010\t\t\t// ctxtsk does not execute FPU instructions.\n"
        "\n"
        "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
        "\tvstmdb\tr2!, {s16-s31}\t\t\t// Push FPU register (S16-S31)\n"
        "\tstmdb\tr2!, {r3}\t\t\t// FPU usage flag\n"
        "#else\n"
        "\tvpush\t{s16-s31}\t\t\t// Push FPU register (S15-S31)\n"
        "\tpush\t{r3}\t\t\t\t//FPU usage flag\n"
        "#endif\n",
        "vstmdb\tr2!, {s16-s31}",
    )

    text, _ = _replace_one_of(
        text,
        path,
        [
            "\tstr\tsp, [r1, #TCB_tskctxb + CTXB_ssp]\t// Save 'ssp' to TCB\n"
        ],
        "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
        "\tstr\tr2, [r1, #TCB_tskctxb + CTXB_ssp]\t// Save PSP to TCB\n"
        "#else\n"
        "\tstr\tsp, [r1, #TCB_tskctxb + CTXB_ssp]\t// Save 'ssp' to TCB\n"
        "#endif\n",
        "Save PSP to TCB",
    )

    text, _ = _replace_one_of(
        text,
        path,
        [
            "\tldr\tr2, =0\n"
            "\tmsr\tbasepri, r2\t\t\t// Enable interruput\n"
        ],
        "\tldr\tr2, =0\n"
        "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
        "\tmrs\tr3, control\n"
        "\tbic\tr3, r3, #4\n"
        "\tmsr\tcontrol, r3\n"
        "\tisb\n"
        "#endif\n"
        "\tmsr\tbasepri, r2\t\t\t// Enable interruput\n",
        "bic\tr3, r3, #4",
    )

    if "Task PSP" not in text:
        restore_olds = [
            "\tstr\tr8, [r0]\t\t\t// ctxtsk = schedtsk\n"
            "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
            "\tcpsid\ti\n"
            "#endif\n"
            "\tldr\tsp, [r8, #TCB_tskctxb + CTXB_ssp]\t// Restore 'ssp' from TCB\n",
            "\tstr\tr8, [r0]\t\t\t// ctxtsk = schedtsk\n"
            "\tldr\tsp, [r8, #TCB_tskctxb + CTXB_ssp]\t// Restore 'ssp' from TCB\n",
        ]
        restore_new = (
            "\tstr\tr8, [r0]\t\t\t// ctxtsk = schedtsk\n"
            "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
            "\tcpsid\ti\n"
            "\tldr\tr2, [r8, #TCB_tskctxb + CTXB_ssp]\t// Task PSP\n"
            "#else\n"
            "\tldr\tsp, [r8, #TCB_tskctxb + CTXB_ssp]\t// Restore 'ssp' from TCB\n"
            "#endif\n"
        )
        found = next((old for old in restore_olds if old in text), None)
        if found is not None:
            if text.count(found) != 1:
                raise PatchError(f"expected 1 ssp restore site in {path}")
            text = text.replace(found, restore_new, 1)
        else:
            ldr_sp = (
                "\tldr\tsp, [r8, #TCB_tskctxb + CTXB_ssp]\t// Restore 'ssp' from TCB\n"
            )
            if ldr_sp not in text or text.count(ldr_sp) != 1:
                raise PatchError(f"PendSV ssp restore site not found in {path}")
            text = text.replace(
                ldr_sp,
                "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
                "\tldr\tr2, [r8, #TCB_tskctxb + CTXB_ssp]\t// Task PSP\n"
                "#else\n"
                + ldr_sp
                + "#endif\n",
                1,
            )

    text, _ = _replace_one_of(
        text,
        path,
        [
            "\tldr\tr3,[sp]\t\t\t\t// load FPU usage flag\n"
        ],
        "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
        "\tldr\tr3, [r2]\t\t\t// load FPU usage flag\n"
        "#else\n"
        "\tldr\tr3,[sp]\t\t\t\t// load FPU usage flag\n"
        "#endif\n",
        "ldr\tr3, [r2]",
    )

    text, _ = _replace_one_of(
        text,
        path,
        [
            "\tpop\t{r3}\n"
            "\tvpop\t{s16-s31}\t\t\t// Pop FPU register (S15-S31)\n"
        ],
        "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
        "\tldmia\tr2!, {r3}\n"
        "\tvldmia\tr2!, {s16-s31}\t\t\t// Pop FPU register (S16-S31)\n"
        "#else\n"
        "\tpop\t{r3}\n"
        "\tvpop\t{s16-s31}\t\t\t// Pop FPU register (S15-S31)\n"
        "#endif\n",
        "vldmia\tr2!, {s16-s31}",
    )

    text, _ = _replace_one_of(
        text,
        path,
        [
            "\tpop\t{lr}\n"
            "\tpop\t{r4-r11}\n"
        ],
        "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
        "\tldmia\tr2!, {lr}\n"
        "\tldmia\tr2!, {r4-r11}\n"
        "\tmsr\tpsp, r2\n"
        "\tisb\n"
        "#else\n"
        "\tpop\t{lr}\n"
        "\tpop\t{r4-r11}\n"
        "#endif\n",
        "msr\tpsp, r2",
    )

    if "bic\tr2, r2, #4\t\t\t// CONTROL.FPCA" not in text:
        old_cpsie_only = (
            "\tmsr\tbasepri, r1\t\t\t// Enable inperrupt\n"
            "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
            "\tcpsie\ti\n"
            "#endif\n"
        )
        new_cpsie = (
            "\tmsr\tbasepri, r1\t\t\t// Enable inperrupt\n"
            "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
            "\tmrs\tr2, control\n"
            "\tbic\tr2, r2, #4\t\t\t// CONTROL.FPCA\n"
            "\tmsr\tcontrol, r2\n"
            "\tisb\n"
            "\tcpsie\ti\n"
            "#endif\n"
        )
        vanilla_tail = (
            "\tmsr\tbasepri, r1\t\t\t// Enable inperrupt\n"
            "\n"
            "\tbx\tlr\n"
        )
        vanilla_new = new_cpsie + "\n\tbx\tlr\n"
        if old_cpsie_only in text:
            if text.count(old_cpsie_only) != 1:
                raise PatchError(f"expected 1 cpsie site in {path}")
            text = text.replace(old_cpsie_only, new_cpsie, 1)
        elif vanilla_tail in text:
            if text.count(vanilla_tail) != 1:
                raise PatchError(f"expected 1 basepri/bx site in {path}")
            text = text.replace(vanilla_tail, vanilla_new, 1)
        else:
            raise PatchError(f"PendSV cpsie site not found in {path}")

    if text == orig:
        return "skip"
    write_text(path, text, nl)
    return "ok"


def patch_cpu_task(mtk3: Path) -> str:
    """SoftDevice: tasks return via PSP so SD IRQs keep the exception MSP."""
    path = mtk3 / "kernel/sysdepend/cpu/core/armv7m/cpu_task.h"
    require(path)
    return replace_once(
        path,
        "\tssp->exp_ret\t= 0xFFFFFFF9;\n",
        "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
        "\t/* Thread mode / PSP. SoftDevice IRQs stay on MSP (exception stack). */\n"
        "\tssp->exp_ret\t= 0xFFFFFFFD;\n"
        "#else\n"
        "\tssp->exp_ret\t= 0xFFFFFFF9;\n"
        "#endif\n",
        skip_if="0xFFFFFFFD",
    )


def patch_reset_hdl(mtk3: Path) -> str:
    path = mtk3 / "kernel/sysdepend/cpu/core/armv7m/reset_hdl.c"
    require(path)
    text, nl = read_text(path)
    changed = False

    old_lspen = "*(_UW*)FPU_FPCCR |= (FPU_FPCCR_ASPEN | FPU_FPCCR_LSPEN);"
    aspen_on = (
        "*(_UW*)FPU_FPCCR |= FPU_FPCCR_ASPEN;\n"
        "\t*(_UW*)FPU_FPCCR &= ~FPU_FPCCR_LSPEN;"
    )
    aspen_off_sd = (
        "#if SOFTDEVICE_PRESENT\n"
        "\t*(_UW*)FPU_FPCCR &= ~(FPU_FPCCR_ASPEN | FPU_FPCCR_LSPEN);\n"
        "#else\n"
        "\t*(_UW*)FPU_FPCCR |= FPU_FPCCR_ASPEN;\n"
        "\t*(_UW*)FPU_FPCCR &= ~FPU_FPCCR_LSPEN;\n"
        "#endif"
    )
    if "FPU_FPCCR_ASPEN | FPU_FPCCR_LSPEN);" in text and old_lspen in text:
        text = text.replace(old_lspen, aspen_off_sd, 1)
        changed = True
    elif aspen_off_sd in text:
        pass
    elif aspen_on in text:
        text = text.replace(aspen_on, aspen_off_sd, 1)
        changed = True
    elif "FPU_FPCCR &= ~FPU_FPCCR_LSPEN" in text:
        pass
    else:
        raise PatchError(f"FPU LSPEN site not found in {path}")

    orig_vtor = (
        "\t/* Set Vector Table offset to SRAM */\n"
        "\t*(_UW*)SCB_VTOR = (UW)exchdr_tbl;\n"
    )
    good_vtor = (
        "#if !SOFTDEVICE_PRESENT\n"
        "\t/* Set Vector Table offset to SRAM */\n"
        "\t*(_UW*)SCB_VTOR = (UW)exchdr_tbl;\n"
        "#endif\n"
    )
    broken_vtor = (
        "\t/* Set Vector Table offset to SRAM */\n"
        "\t#if !SOFTDEVICE_PRESENT\n"
        "\t*(_UW*)SCB_VTOR = (UW)exchdr_tbl;\n"
        "#endif\n"
    )
    doubled_vtor = (
        "#if !SOFTDEVICE_PRESENT\n"
        "#if !SOFTDEVICE_PRESENT\n"
        "\t/* Set Vector Table offset to SRAM */\n"
        "\t*(_UW*)SCB_VTOR = (UW)exchdr_tbl;\n"
        "#endif\n"
        "#endif\n"
    )
    if doubled_vtor in text:
        text = text.replace(doubled_vtor, good_vtor, 1)
        changed = True
    elif broken_vtor in text:
        text = text.replace(broken_vtor, good_vtor, 1)
        changed = True
    elif good_vtor in text:
        pass
    elif orig_vtor in text:
        text = text.replace(orig_vtor, good_vtor, 1)
        changed = True
    else:
        raise PatchError(f"VTOR site not found in {path}")

    # tkernel_ble_wsd.ld + INSERT AFTER lets the default ld script overwrite
    # __data_start / __bss_start with flash addresses. Use the RAM symbols
    # that BridgingEdgeAI uses (__data_start__, _bss_start).
    if "IMPORT	const void *__data_start__;" not in text:
        old_imp = "IMPORT	const void *__data_start;"
        if old_imp not in text:
            raise PatchError(f"__data_start import not found in {path}")
        text = text.replace(old_imp, "IMPORT	const void *__data_start__;", 1)
        changed = True
    if "IMPORT	const void *_bss_start;" not in text:
        old_imp = "IMPORT	const void *__bss_start;"
        if old_imp not in text:
            raise PatchError(f"__bss_start import not found in {path}")
        text = text.replace(old_imp, "IMPORT	const void *_bss_start;", 1)
        changed = True
    if "top = (UW*)&__data_start__;" not in text:
        old_data = "top = (UW*)&__data_start;"
        if old_data not in text:
            raise PatchError(f"__data_start copy site not found in {path}")
        text = text.replace(old_data, "top = (UW*)&__data_start__;", 1)
        changed = True
    if "src = (UW*)&__data_org;;" in text:
        text = text.replace("src = (UW*)&__data_org;;", "src = (UW*)&__data_org;", 1)
        changed = True
    if "top = (UW*)&_bss_start;" not in text:
        old_bss = "top = (UW*)&__bss_start;"
        if old_bss not in text:
            raise PatchError(f"__bss_start zero site not found in {path}")
        text = text.replace(old_bss, "top = (UW*)&_bss_start;", 1)
        changed = True
    old_bss_len = "for(i = ((INT)&__bss_end - (INT)&__bss_start)/sizeof(UW); i > 0 ; i--) {"
    new_bss_len = "for(i = ((INT)&__bss_end - (INT)top)/sizeof(UW); i > 0 ; i--) {"
    if old_bss_len in text:
        text = text.replace(old_bss_len, new_bss_len, 1)
        changed = True
    elif new_bss_len not in text:
        # already using _bss_start in the length expression is also fine
        if "((INT)&__bss_end - (INT)&_bss_start)" not in text:
            raise PatchError(f"bss length site not found in {path}")

    if not changed:
        return "skip"
    write_text(path, text, nl)
    return "ok"


def patch_config_h(mtk3: Path) -> str:
    path = mtk3 / "config/config.h"
    require(path)
    text, nl = read_text(path)
    orig = text
    import re

    def set_stack(name: str, value: str) -> None:
        nonlocal text
        m = re.search(rf"#define\s+{name}\s+\(\s*(\d+)\s*\)", text)
        if m is None:
            raise PatchError(f"{name} not found in {path}")
        if m.group(1) == value:
            return
        text, n = re.subn(
            rf"(#define\s+{name}\s+)\(\s*\d+\s*\)",
            rf"\1({value})",
            text,
            count=1,
        )
        if n != 1:
            raise PatchError(f"{name} replace failed in {path}")

    set_stack("CNF_EXC_STACK_SIZE", "4096")
    set_stack("CNF_TMP_STACK_SIZE", "2048")
    if text == orig:
        return "skip"
    # Keep the exception-stack comment in sync when we bump the size.
    text = text.replace(
        "#define CNF_EXC_STACK_SIZE\t(4096)\t/* Exception stack size */",
        "#define CNF_EXC_STACK_SIZE\t(4096)\t/* Exception stack (SoftDevice IRQs use MSP) */",
        1,
    )
    write_text(path, text, nl)
    return "ok"


def patch_makefile(mtk3: Path) -> str:
    path = mtk3 / "build_make/makefile"
    require(path)
    r1 = insert_after(
        path,
        "ifeq ($(TARGET), _MICROBIT_)\ninclude microbit.mk\nendif\n",
        "\n-include blenus_overlay.mk\n",
        "blenus_overlay.mk",
    )
    text, nl = read_text(path)
    changed = False

    if "$(EXE_FILE).hex" not in text.split("all:", 1)[-1][:80]:
        old_all = "all: $(EXE_FILE).elf\n"
        if old_all not in text:
            raise PatchError(f"all: target not found in {path}")
        text = text.replace(old_all, "all: $(EXE_FILE).elf $(EXE_FILE).hex\n", 1)
        changed = True

    if "$(EXTOBJS)" not in text:
        old_link = (
            '\t$(LINK)  $(LFLAGS) -T $(LNKFILE) -Wl,-Map,"$(EXE_FILE).map"'
            ' -o "$(EXE_FILE).elf" $(OBJS)\n'
        )
        new_link = (
            '\t$(LINK)  $(LFLAGS) -T $(LNKFILE) -Wl,-Map,"$(EXE_FILE).map"'
            ' -o "$(EXE_FILE).elf" $(OBJS) $(EXTOBJS)\n'
        )
        if old_link not in text:
            raise PatchError(f"link line not found in {path}")
        text = text.replace(old_link, new_link, 1)
        changed = True

    hex_rule = (
        '$(EXE_FILE).hex: $(EXE_FILE).elf\n'
        '\t@echo \'Converting to hex: $@\'\n'
        '\t$(OBJCOPY) -O ihex "$(EXE_FILE).elf" "$(EXE_FILE).hex"\n'
        '\t@echo \'Finished Converting: $@\'\n'
        '\t@echo \' \'\n'
        '\n'
    )
    if "$(EXE_FILE).hex: $(EXE_FILE).elf" not in text:
        marker = "$(EXE_FILE).elf: $(OBJS)\n"
        if marker not in text:
            raise PatchError(f"elf rule not found in {path}")
        text = text.replace(marker, hex_rule + marker, 1)
        changed = True

    old_clean = (
        "\t-$(RM) $(OBJS) $(SECONDARY_SIZE) $(ASM_DEPS) $(S_UPPER_DEPS)"
        " $(C_DEPS) $(EXE_FILE).elf $(EXE_FILE).map\n"
    )
    new_clean = (
        "\t-$(RM) $(OBJS) $(SECONDARY_SIZE) $(ASM_DEPS) $(S_UPPER_DEPS)"
        " $(C_DEPS) $(EXE_FILE).elf $(EXE_FILE).hex $(EXE_FILE).map\n"
    )
    if old_clean in text:
        text = text.replace(old_clean, new_clean, 1)
        changed = True

    if changed:
        write_text(path, text, nl)
    if r1 == "skip" and not changed:
        return "skip"
    return "ok"


def patch_config_device(mtk3: Path) -> str:
    path = mtk3 / "config/config_device.h"
    require(path)
    text, nl = read_text(path)
    if "DEVCNF_USE_BLENUS" in text:
        return "skip"
    import re

    new, n = re.subn(
        r"(#define\s+DEVCNF_USE_IIC\s+\d+[^\n]*\n)",
        r"\1#define DEVCNF_USE_BLENUS	1		// BLE NUS (SoftDevice) device\n",
        text,
        count=1,
    )
    if n != 1:
        raise PatchError(f"DEVCNF_USE_IIC not found in {path}")
    write_text(path, new, nl)
    return "ok"


def patch_device_h(mtk3: Path) -> str:
    path = mtk3 / "device/include/device.h"
    require(path)
    return insert_after(
        path,
        "#endif	/* DEVCNF_USE_IIC */\n",
        "\n#if DEVCNF_USE_BLENUS		/* Use BLE NUS device */\n"
        "#include \"dev_blenus.h\"\n"
        "#endif	/* DEVCNF_USE_BLENUS */\n",
        "dev_blenus.h",
    )


def patch_dev_def(mtk3: Path) -> str:
    path = mtk3 / "device/include/dev_def.h"
    require(path)
    text, nl = read_text(path)
    if "DEV_BLENUS_ENABLE" in text:
        return "skip"
    iic_endif = "#endif	/* DEVCNF_USE_IIC */\n"
    iic_else = "#define DEV_IIC_ENABLE		0		// I2C communication device\n"
    must_contain(path, text, iic_endif)
    must_contain(path, text, iic_else)
    text = text.replace(
        iic_endif,
        iic_endif
        + "\n"
        + "#if DEVCNF_USE_BLENUS && DEV_BLENUS_UNITNM	/* Use BLE NUS device */\n"
        + "#define DEV_BLENUS_ENABLE	1\n"
        + "#else\n"
        + "#define DEV_BLENUS_ENABLE	0\n"
        + "#endif	/* DEVCNF_USE_BLENUS */\n",
        1,
    )
    text = text.replace(
        iic_else,
        iic_else + "#define DEV_BLENUS_ENABLE	0		// BLE NUS device\n",
        1,
    )
    if text.count("DEV_BLENUS_ENABLE") < 2:
        raise PatchError(f"DEV_BLENUS_ENABLE insert failed in {path}")
    write_text(path, text, nl)
    return "ok"


def patch_devinit(mtk3: Path) -> str:
    path = mtk3 / "kernel/sysdepend/microbit/devinit.c"
    require(path)
    return insert_after(
        path,
        "\terr = dev_init_ser(0);\n"
        "\tif(err < E_OK) return err;\n"
        "#endif\n",
        "\n"
        "#if DEVCNF_USE_BLENUS\n"
        "\terr = dev_init_blenus(0);\n"
        "\tif(err < E_OK) return err;\n"
        "#endif\n",
        "dev_init_blenus",
    )


def patch_interrupt_c(mtk3: Path) -> str:
    path = mtk3 / "kernel/sysdepend/cpu/core/armv7m/interrupt.c"
    require(path)
    text, nl = read_text(path)
    if "knl_fpca_clear" in text:
        return "skip"

    helper = (
        "\n"
        "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
        "LOCAL void knl_fpca_clear(void)\n"
        "{\n"
        "\tUW\tcontrol;\n"
        "\n"
        "\tAsm(\"mrs %0, control\" : \"=r\"(control));\n"
        "\tcontrol &= ~(1u << 2);\n"
        "\tAsm(\"msr control, %0\" :: \"r\"(control) : \"memory\");\n"
        "\tAsm(\"isb\" ::: \"memory\");\n"
        "}\n"
        "#endif\n"
    )
    marker = "Noinit(LOCAL FP knl_inthdr_tbl[N_INTVEC]);\t/* HLL Interrupt Handler Table */\n"
    if marker not in text:
        raise PatchError(f"inthdr table marker not found in {path}")
    text = text.replace(marker, marker + helper, 1)

    hll = (
        "EXPORT void knl_hll_inthdr(void)\n"
        "{\n"
        "\tFP\tinthdr;\n"
        "\tUW\tintno;\n"
        "\n"
        "\tENTER_TASK_INDEPENDENT;\n"
    )
    hll_new = (
        "EXPORT void knl_hll_inthdr(void)\n"
        "{\n"
        "\tFP\tinthdr;\n"
        "\tUW\tintno;\n"
        "\n"
        "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
        "\tknl_fpca_clear();\n"
        "#endif\n"
        "\tENTER_TASK_INDEPENDENT;\n"
    )
    if hll not in text:
        raise PatchError(f"knl_hll_inthdr site not found in {path}")
    text = text.replace(hll, hll_new, 1)

    systim = (
        "EXPORT void knl_systim_inthdr(void)\n"
        "{\n"
        "\tENTER_TASK_INDEPENDENT;\n"
    )
    systim_new = (
        "EXPORT void knl_systim_inthdr(void)\n"
        "{\n"
        "#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT\n"
        "\tknl_fpca_clear();\n"
        "#endif\n"
        "\tENTER_TASK_INDEPENDENT;\n"
    )
    if systim not in text:
        raise PatchError(f"knl_systim_inthdr site not found in {path}")
    text = text.replace(systim, systim_new, 1)

    write_text(path, text, nl)
    return "ok"


def patch_vector_tbl(mtk3: Path) -> str:
    path = mtk3 / "kernel/sysdepend/cpu/nrf5/vector_tbl.c"
    require(path)
    return replace_once(
        path,
        "\tDefault_Handler,\t\t\t/* IRQ 22 */\n",
        "\tSWI2_EGU2_IRQHandler,\t\t\t/* IRQ 22 SoftDevice events */\n",
        skip_if="SWI2_EGU2_IRQHandler",
    )


def patch_sysdepend_h(mtk3: Path) -> str:
    path = mtk3 / "kernel/sysdepend/cpu/core/armv7m/sysdepend.h"
    require(path)
    return insert_after(
        path,
        "IMPORT void Default_Handler(void);\t\t/* Default Handler (Undefine Interrupt) */\n",
        "IMPORT void SWI2_EGU2_IRQHandler(void);\t\t/* SoftDevice event IRQ (SWI2) */\n",
        "SWI2_EGU2_IRQHandler",
    )


def patch_sysdef_h(mtk3: Path) -> str:
    path = mtk3 / "include/sys/sysdepend/cpu/nrf5/sysdef.h"
    require(path)
    return replace_once(
        path,
        "#define INTPRI_SYSTICK\t\t1\t/* SysTick */\n",
        "#define INTPRI_SYSTICK\t\t6\t/* SysTick: must not preempt SoftDevice SWI2 (pri 6) */\n",
        skip_if="must not preempt SoftDevice SWI2",
    )


# PMC's μT-Kernel 3.0 source files each carry this header block, ending in
# "Released by TRON Forum(...)." followed by a closing comment divider and
# "*/". We anchor the "modified by benus" note right after that block so it
# never precedes or splits PMC's own copyright notice.
PMC_HEADER_MARKER = "Released by TRON Forum(http://www.tron.org)"
SOURCE_NOTE_MARKER = 'Modified for the "benus" project'

SOURCE_NOTE_C = (
    "\n"
    "/*\n"
    ' * NOTE: Modified for the "benus" project (BLE NUS driver for micro:bit v2\n'
    " * / μT-Kernel 3.0). Author: koh aiaida (koh@aiaida.jp).\n"
    " * See https://github.com/koh523/benus for details.\n"
    " */\n"
)

SOURCE_NOTE_MK = (
    "\n"
    '# NOTE: Modified for the "benus" project (BLE NUS driver for micro:bit v2\n'
    "# / μT-Kernel 3.0). Author: koh aiaida (koh@aiaida.jp).\n"
    "# See https://github.com/koh523/benus for details.\n"
)


def add_source_note_c(path: Path) -> str:
    text, nl = read_text(path)
    if SOURCE_NOTE_MARKER in text:
        return "skip"
    idx = text.find(PMC_HEADER_MARKER)
    if idx == -1:
        raise PatchError(f"PMC header marker not found in {path}")
    end = text.find("*/", idx)
    if end == -1:
        raise PatchError(f"PMC header end not found in {path}")
    end += len("*/")
    nl_pos = text.find("\n", end)
    insert_at = nl_pos + 1 if nl_pos != -1 else end
    write_text(path, text[:insert_at] + SOURCE_NOTE_C + text[insert_at:], nl)
    return "ok"


def add_source_note_makefile(path: Path) -> str:
    text, nl = read_text(path)
    if SOURCE_NOTE_MARKER in text:
        return "skip"
    divider = "################################################################################\n"
    idx = text.find(divider)
    if idx == -1:
        raise PatchError(f"makefile header start not found in {path}")
    idx2 = text.find(divider, idx + len(divider))
    if idx2 == -1:
        raise PatchError(f"makefile header end not found in {path}")
    insert_at = idx2 + len(divider)
    write_text(path, text[:insert_at] + SOURCE_NOTE_MK + text[insert_at:], nl)
    return "ok"


PATCH_STEPS = [
    ("dispatch.S", patch_dispatch, "kernel/sysdepend/cpu/core/armv7m/dispatch.S"),
    ("cpu_task.h", patch_cpu_task, "kernel/sysdepend/cpu/core/armv7m/cpu_task.h"),
    ("reset_hdl.c", patch_reset_hdl, "kernel/sysdepend/cpu/core/armv7m/reset_hdl.c"),
    ("config.h", patch_config_h, "config/config.h"),
    ("makefile", patch_makefile, "build_make/makefile"),
    ("config_device.h", patch_config_device, "config/config_device.h"),
    ("device.h", patch_device_h, "device/include/device.h"),
    ("dev_def.h", patch_dev_def, "device/include/dev_def.h"),
    ("devinit.c", patch_devinit, "kernel/sysdepend/microbit/devinit.c"),
    ("interrupt.c", patch_interrupt_c, "kernel/sysdepend/cpu/core/armv7m/interrupt.c"),
    ("vector_tbl.c", patch_vector_tbl, "kernel/sysdepend/cpu/nrf5/vector_tbl.c"),
    ("sysdepend.h", patch_sysdepend_h, "kernel/sysdepend/cpu/core/armv7m/sysdepend.h"),
    ("sysdef.h", patch_sysdef_h, "include/sys/sysdepend/cpu/nrf5/sysdef.h"),
]


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Copy blenus overlay into a μT-Kernel tree and apply host patches"
    )
    ap.add_argument(
        "--mtk3",
        required=True,
        help="μT-Kernel root (mtkernel_3), or its parent if it contains mtkernel_3/",
    )
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    src_root = repo_root()
    mtk3 = find_mtk3(Path(args.mtk3).resolve())
    if not is_mtk3_tree(mtk3):
        print(
            f"Not a μT-Kernel tree (missing config/config.h or build_make/makefile): {mtk3}",
            file=sys.stderr,
        )
        return 1

    if args.dry_run:
        print(f"dry-run: would install into {mtk3}")
        print("copy:")
        for rel, _ in COPY_TREES:
            print(f"  {rel}")
        for rel in COPY_FILES:
            print(f"  {rel}")
        print("patch:")
        for name, _, _ in PATCH_STEPS:
            print(f"  {name}")
        return 0

    failed = 0
    try:
        for name, status in install_overlay(src_root, mtk3):
            print(f"{status:4}  copy {name}")
    except PatchError as e:
        print(f"FAIL  copy: {e}", file=sys.stderr)
        return 1

    for name, fn, rel_path in PATCH_STEPS:
        try:
            status = fn(mtk3)
            print(f"{status:4}  {name}")
            if status == "ok":
                note_fn = add_source_note_makefile if name == "makefile" else add_source_note_c
                note_status = note_fn(mtk3 / rel_path)
                print(f"{note_status:4}  {name} (note)")
        except PatchError as e:
            print(f"FAIL  {name}: {e}", file=sys.stderr)
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
