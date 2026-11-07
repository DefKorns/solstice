#!/bin/sh
#  Copyright (C) 2026 DefKorns (https://defkorns.github.io/LICENSE)
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <https://www.gnu.org/licenses/>.
set -e

src=${1:-$(pwd)}
out=${2:-$src/output}
mali=modules/mali/DX910-SW-99002-r4p0-00rel0/driver/src/devicedrv/mali

export PATH="/opt/toolchain/bin:${sntool_dir:+$sntool_dir:}$PATH"
export ARCH=arm CROSS_COMPILE=arm-cortex_a7-linux-gnueabihf-

cd "$src"
make sun_nontendocm_solstice_defconfig
make -j"$(nproc)" zImage modules
release=$(cat include/config/kernel.release)

make -C "$mali" USING_MMU=1 USING_UMP=0 USING_PMM=1 BUILD=release KDIR="$src"
make -C "$src" M="$src/clovercon" CFLAGS_MODULE=-fno-pic modules

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
make INSTALL_MOD_PATH="$stage" DEPMOD=true modules_install
modules=$stage/lib/modules/$release
mkdir -p "$modules/extra"
cp "$mali/mali.ko" clovercon/clovercon.ko "$modules/extra/"
rm -f "$modules/build" "$modules/source"
find "$modules" -name '*.ko' -exec "${CROSS_COMPILE}strip" --strip-unneeded {} +
cp modules.builtin "$modules/"

mkdir -p "$out"
cp arch/arm/boot/zImage "$out/zImage"
echo "$release" > "$out/release"
tar -C "$stage/lib/modules" -cf "$out/modules.tar" "$release"
echo "$release: $(find "$modules" -name '*.ko' | wc -l) modules, zImage $(wc -c < "$out/zImage") bytes"
