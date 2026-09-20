# blenus/benus

[日本語](README.md) | **English**

blenus is a device driver that lets you use the Nordic UART Service (NUS) over BLE from μT-Kernel 3.0 (micro:bit v2) through `tk_*_dev`.  
benus is a package that applies blenus to μT-Kernel 3.0 for micro:bit.

## Background

Microsoft MakeCode, which is widely used with micro:bit, supports wireless communication over BLE out of the box. On the other hand, in μT-Kernel 3.0 for micro:bit provided by Personal Media Corporation (hereafter PMC), the only means of communicating with the outside is USB serial.

If wireless were available on μT-Kernel as well, tasks could exchange their status and send sensor values without a cable to a PC. This would broaden the situations in which micro:bit is used as an execution environment for μT-Kernel, and raise its value.

However, the BLE protocol stack is provided by Nordic as a binary called SoftDevice. Its API is public, but its implementation is a black box. It can only be called via SVC (supervisor call), which makes it hard to handle directly from a μT-Kernel application.

blenus packages that SoftDevice and the Nordic UART Service (NUS) as a μT-Kernel device driver, so that they can be used through the existing device APIs such as `tk_opn_dev` / `tk_swri_dev`. An application can handle wireless communication with a feel close to serial communication, without being aware of the details of the BLE stack.

This repository (benus) contains the driver and the SoftDevice-related parts, patches for the host tree, a usage example (`app_sample`), and a Web application for checking operation (`web/`). The μT-Kernel distributed by PMC (the kernel proper, including the micro:bit-dependent parts) is not included. First extract PMC's `mtkernel_3`, then place the contents of this repository in it, apply the patches, and build.

## Prerequisites

The target board is **micro:bit v2**. It does not work on v1.  
The host PC is assumed to have the development tools described in [“micro:bitでμT-Kernel 3.0を動かそう” (Let's run μT-Kernel 3.0 on micro:bit), Part 2 (preparing the development tools and compiling)](https://www.t-engine4u.com/info/mbit/2.html) (the page is in Japanese). A GNU Arm Embedded Toolchain, make (on Windows, e.g. xPack Windows Build Tools) and Python 3.8 or later are required. Eclipse is optional; building from the command line in `build_make` is also fine.  
This guide assumes operation in a Git Bash environment.

## Setup (extract PMC → clone → placement and patches)

Choose an empty directory as the working directory. PMC's `mtkernel_3` is the root of the build. Clone this repository separately from it.

### 1. Extract PMC's μT-Kernel

Obtain `362_mbit_mtk3.zip` from [the web page “micro:bitでμT-Kernel 3.0を動かそう”](https://www.t-engine4u.com/info/mbit/2.html) and extract it. Below, `362_mbit_mtk3/mtkernel_3` is called the top folder.

Under `mtkernel_3`, check that the following paths are present:

- `kernel/`
- `lib/`
- `device/`
- `include/`
- `config/`
- `etc/`
- `app_sample/`
- `build_make/`

### 2. Clone this repository

Clone it outside `mtkernel_3`. Do not clone it inside the `mtkernel_3` source tree.

```bash
git clone https://github.com/koh523/benus.git
```

At this point, `device/`, `components/`, `etc/`, `patch/`, `build_make/` and `app_sample/` are placed on the local side.

### 3. Place the driver source tree and apply the patches

Pass the extracted `mtkernel_3` to the script on the clone side. Run either one of the following (you do not need both).
Pass to `--mtk3` the directory that contains `config/` and `build_make/makefile`.

```bash
# Run it directly with Python
python benus/patch/apply.py --mtk3 /path/to/mtkernel_3

# Or run it through the shell script
./benus/patch/apply.sh --mtk3 /path/to/mtkernel_3
```

`apply.sh` is a wrapper that looks for a usable Python (`python`, `python3`, `py`) and runs `apply.py`. The result is the same whichever you use.


The following is placed. PMC's `app_sample` is replaced by this repository's usage example.

| Placed                                          | Contents                                                        |
| ----------------------------------------------- | --------------------------------------------------------------- |
| `app_sample/`                                   | Usage example: NUS echo and a blinking LED (replaces the PMC sample) |
| `device/blenus/`, `device/include/dev_blenus.h` | Device driver                                                   |
| `components/`                                   | SoftDevice / nRF5 SDK / BLE                                     |
| `build_make/blenus_overlay.mk` and others       | Makefile for the overlay                                        |
| `etc/linker/microbit/tkernel_ble_wsd.ld`        | Linker script for SoftDevice                                    |

Next, the targets of the patches applied to the μT-Kernel side are as follows. See `patch/` for the contents of the changes.

- `kernel/sysdepend/cpu/core/armv7m/dispatch.S`
- `kernel/sysdepend/cpu/core/armv7m/cpu_task.h`
- `kernel/sysdepend/cpu/core/armv7m/reset_hdl.c`
- `kernel/sysdepend/cpu/core/armv7m/interrupt.c`
- `kernel/sysdepend/cpu/core/armv7m/sysdepend.h`
- `kernel/sysdepend/cpu/nrf5/vector_tbl.c`
- `kernel/sysdepend/microbit/devinit.c`
- `config/config.h`
- `config/config_device.h`
- `build_make/makefile`
- `device/include/device.h`
- `device/include/dev_def.h`
- `include/sys/sysdepend/cpu/nrf5/sysdef.h`

`blenus_overlay.mk` is a new file of this repository. PMC's `makefile` / `microbit.mk` are not replaced.

### 4. Build

Follow PMC's build procedure (Eclipse or `build_make`) and build on the `mtkernel_3` side. From the command line, move to `build_make` and run `make`.

```bash
cd /path/to/mtkernel_3/build_make
make
```

On success, `mtkernel_3.elf` is generated in the same directory.

### 5. Flash to the micro:bit

Flash the ELF generated in step 4.

```bash
pyocd load -t nrf52 mtkernel_3.elf
```

The application is the placed `app_sample`. `tk_opn_dev("blua")` starts SoftDevice and advertising.

### Supplement (checking the operation)

To check the operation of `app_sample`, use `web/webbt_nus-simple.html` of this repository. After flashing, the micro:bit advertises as `micro:bit2_UART`.

Open `web/webbt_nus-simple.html` in Chrome, connect, and check sending and receiving.
