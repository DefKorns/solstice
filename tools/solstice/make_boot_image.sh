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

zimage=$1
modules_tar=$2
stock_img=$3
out=$4
max_size=4980736

if [ ! -f "$zimage" ] || [ ! -f "$modules_tar" ] || [ ! -f "$stock_img" ] || [ -z "$out" ]; then
  echo "usage: $0 zImage modules.tar stock_boot.img out.img" 1>&2
  exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

release=$(tar -tf "$modules_tar" | head -n 1 | cut -d/ -f1)
slim=$work/slim/lib/modules/$release
mkdir -p "$work/full/lib/modules" "$slim/kernel/drivers/block/nand" "$slim/kernel/drivers/scsi" "$work/rd" "$work/stock"

tar -xf "$modules_tar" -C "$work/full/lib/modules"
cp "$work/full/lib/modules/$release/kernel/drivers/block/nand/nand.ko" "$slim/kernel/drivers/block/nand/"
cp "$work/full/lib/modules/$release/kernel/drivers/scsi/sd_mod.ko" "$slim/kernel/drivers/scsi/"
depmod -b "$work/slim" "$release" 2> /dev/null

unpackbootimg -i "$stock_img" -o "$work/stock/" > /dev/null
prefix=$work/stock/$(basename "$stock_img")

xz -dc "${prefix}-ramdisk.gz" | (cd "$work/rd" && cpio -idm --quiet)
rm -rf "$work/rd/lib32/modules-ramfs"
mkdir -p "$work/rd/lib32/modules-ramfs"
cp -a "$slim/." "$work/rd/lib32/modules-ramfs/"
(cd "$work/rd" && find . | cpio -o -H newc --quiet | xz --check=crc32 --lzma2=preset=9e,dict=4MiB) > "$work/ramdisk.new"

mkbootimg --kernel "$zimage" --ramdisk "$work/ramdisk.new" \
  --cmdline "$(cat "${prefix}-cmdline")" \
  --base "0x$(cat "${prefix}-base")" \
  --pagesize "$(cat "${prefix}-pagesize")" \
  --kernel_offset "0x$(cat "${prefix}-kerneloff")" \
  --ramdisk_offset "0x$(cat "${prefix}-ramdiskoff")" \
  --second_offset "0x$(cat "${prefix}-secondoff")" \
  --tags_offset "0x$(cat "${prefix}-tagsoff")" \
  --hash sha1 -o "$out"

size=$(wc -c < "$out")
echo "$out: $size bytes"
[ "$size" -le "$max_size" ] || {
  echo "image exceeds the $max_size byte boot2 slot" 1>&2
  exit 1
}
