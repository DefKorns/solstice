# Solstice tools

Build the Solstice kernel and turn it into something a NES/SNES Classic running hakchi can boot or flash, with one script.

## Why Solstice

The NES/SNES Classic ships a Linux 3.4.113 kernel from Allwinner's R16 BSP, and nobody maintains it. Solstice is a fork of madmonkey1907's source for that kernel (`master` here is upstream, `solstice` is the fork) that keeps the same hardware support and ABI, so hakchi and the existing mods keep working, and adds what a long-lived console is missing:

- **Security fixes.** 36 upstream stable fixes backported from later 3.x releases, in USB, HID, Bluetooth, cfg80211, ALSA, ext4, futex, perf, packet sockets and others. Each one applied cleanly; the ones that conflict with the BSP code were left out on purpose (about 95 so far), not forced in.
- **Hardening.** The stack protector is enabled and `dmesg` is restricted to root.
- **The "Bad page state" bug.** hakchi maps 128 KiB at `0x43000000` through `/dev/mem`, but the stock kernel reserved only part of that window, so free pages inside it got flagged and a later allocation could crash on them. Solstice reserves the whole window.
- **Power switch parity.** The power switch is registered as `gpio-keys` on `/dev/input/event2`, as on the stock kernel, because third-party mods hardcode that device. The headset and knob input drivers, which this console has no use for, are gone.

It is a drop-in kernel: the same cmdline, the same module layout and the same Wi-Fi driver (8188eu) as the stock one, flashed into the same NAND slot. Not done yet: a DualShock 4 driver (`hid-sony`) and a comparison of the Realtek drivers with newer upstream.

```sh
tools/solstice/solstice.sh [-o output_dir] [-c console_host] [-i hakchi2ce_dir]
```

| Option | Meaning |
| --- | --- |
| `-o output_dir` | Where the results go. Default: `tools/solstice/output`. |
| `-c console_host` | After building, boot `solstice.img` on the console from RAM and wait for it to answer. |
| `-i hakchi2ce_dir` | After building, copy `hakchi-latest.hmod` to `<hakchi2ce_dir>/data/`, keeping the previous one as `.previous`. |

## Requirements

- **Docker**, running. On Linux the `qemu-user` binfmt handlers must also be installed, because the kernel build runs hakchi's ARM `sntool` under emulation (Docker Desktop already has them).
- **A POSIX shell with `git`**: Git Bash on Windows, any shell on Linux or macOS.
- **`basehmods.tar`**, copied from the Hakchi2-CE data folder (`data/hakchi2ce`, next to `hakchi.exe`) into this folder, next to `solstice.sh`. The script reads `hakchi.hmod` from it: the stock boot image, the hakchi tools and the `sntool` the kernel build needs. Git ignores it.
- For `-c`: the console on, in hakchi mode and reachable over ssh (USB `169.254.13.37`, or the Wi-Fi address).

## Quick start

Build and boot the new kernel from RAM, which is the safe way to test because a reboot returns to whatever is flashed:

```sh
tools/solstice/solstice.sh -c 192.168.3.197
```

Check `uname -r`, `dmesg`, `/proc/bus/input/devices` and `lsmod` on the console. When it is good, prepare the flash:

```sh
tools/solstice/solstice.sh -i /path/to/hakchi2ce
```

Then power the console off, put it in FEL mode (hold Reset while plugging in the USB cable) and run **Install/Repair** in Hakchi2-CE. After the reboot, a kernel command line without `hakchi-chainload` means the console booted from the NAND.

## What you get

| File | Use |
| --- | --- |
| `solstice.img` | Boot image for a RAM boot. It carries only `nand.ko` and `sd_mod.ko` in the ramdisk and fits the boot2 slot. |
| `hakchi-latest.hmod` | `hakchi.hmod` with the new kernel and the full module set, for flashing. Its `boot/boot.img` is byte-identical to `solstice.img`. |
| `zImage`, `modules.tar`, `release` | The raw build results. |

## How it works

1. Exports `HEAD` plus your uncommitted changes to tracked files into a Docker volume (`solstice-build`). New files need `git add` to be picked up.
2. In the ARM toolchain container (`ghcr.io/defkorns/classicmini-cross-toolchain:jessie-armhf`), extracts `sntool` from the base `hakchi.hmod` and runs `build_kernel.sh`: the defconfig, `zImage`, modules, and the out-of-tree Mali and Clovercon modules.
3. In a small packaging image built from the [Dockerfile](Dockerfile), runs `make_hakchi_hmod.sh`, which calls `make_boot_image.sh`, and unpacks `solstice.img` from the result.
4. Copies everything to the output folder, then runs the `-c` and `-i` steps if asked.

The volume is a cache between runs. `docker volume rm solstice-build` clears it.

## Flashing, and going back

Hakchi2-CE uses `data/hakchi-latest.hmod` while it is newer than the `hakchi.hmod` inside `basehmods.tar` and than the copy on hakchi.net, so it does not replace it when it checks for updates. Install/Repair then writes the kernel to the NAND.

To go back to the stock hakchi kernel, delete `data/hakchi-latest.hmod`, put the console in FEL mode and run Install/Repair again. A full NAND dump taken beforehand is the last resort.

## Things that will bite

- **Slot size.** The kernel is written at NAND sector 30 and hakchi keeps the backup of the stock kernel from sector 68, which leaves 4 980 736 bytes (38 sectors of 128 KiB). The stock hakchi image has 4 585 472 bytes and the Solstice one about 4 715 000. `make_boot_image.sh` fails if the image does not fit.
- **Module folder.** The `hakchi.hmod` install syncs `lib/modules` with `--delete`, so the Solstice modules must be inside it. Do not install a separate hmod whose uninstall removes that folder.
- **Entry names.** Hakchi2-CE looks up `boot/boot.img` by its exact name inside the hmod. `make_hakchi_hmod.sh` stores the entries without a `./` prefix; an archive made with `tar -c .` is rejected ("Sequence contains no elements").
- **A console that is already up.** If the console is running a hakchi shell, Hakchi2-CE tries to use it instead of waiting for FEL. Power it off first.
- **FEL.** If Windows does not list a device with `VID_1F3A&PID_EFE8` when you plug the console in, FEL is not active and Install/Repair will wait forever.
- **`/dev/mem` window.** hakchi maps 128 KiB at `0x43000000`; the kernel reserves all of it, otherwise buddy pages inside it trigger "Bad page state".
- **Windows paths.** `solstice.sh` already deals with MSYS path conversion and with `core.autocrlf`; run it from Git Bash and not from cmd or PowerShell.

## The pieces

`solstice.sh` calls these, and each can be run alone in a suitable environment.

| Script | Arguments | Result |
| --- | --- | --- |
| `build_kernel.sh` | `[src] [out]` | `zImage`, `release` and `modules.tar`. Needs the ARM GCC 4.9.4 toolchain in `/opt/toolchain`; `sntool_dir` points to a folder with a runnable `sntool`. |
| `make_boot_image.sh` | `zImage modules.tar stock_boot.img out.img` | Boot image with the stock ramdisk and command line and only `nand.ko` and `sd_mod.ko` inside. |
| `make_hakchi_hmod.sh` | `base_hakchi.hmod zImage modules.tar out.hmod` | `hakchi.hmod` with the new boot image and `lib/modules`. |
| `memboot.sh` | `console_host boot.img [expected_release]` | Uploads the image, checks its md5 and `sntool check`, boots it through kexec and waits. With a release, it fails if the console comes back on another kernel. |
| `Dockerfile` | | The packaging image: `cpio`, `xz`, `depmod`, `mkbootimg` and `unpackbootimg`. |
