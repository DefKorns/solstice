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

base_hmod=$1
zimage=$2
modules_tar=$3
out=$4
here=$(dirname "$0")

if [ ! -f "$base_hmod" ] || [ ! -f "$zimage" ] || [ ! -f "$modules_tar" ] || [ -z "$out" ]; then
  echo "usage: $0 base_hakchi.hmod zImage modules.tar out_hakchi-latest.hmod" 1>&2
  exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

release=$(tar -tf "$modules_tar" | head -n 1 | cut -d/ -f1)
mkdir -p "$work/hmod" "$work/full/lib/modules"
tar -xzf "$base_hmod" -C "$work/hmod"

"$here/make_boot_image.sh" "$zimage" "$modules_tar" "$work/hmod/boot/boot.img" "$work/boot.img"

tar -xf "$modules_tar" -C "$work/full/lib/modules"
depmod -b "$work/full" "$release" 2> /dev/null
rm -f "$work/full/lib/modules/$release/build" "$work/full/lib/modules/$release/source"
rm -rf "$work/hmod/lib/modules"
mkdir -p "$work/hmod/lib/modules"
cp -a "$work/full/lib/modules/$release" "$work/hmod/lib/modules/"
cp "$work/boot.img" "$work/hmod/boot/boot.img"

(cd "$work/hmod" && ls -A > "$work/entries" && tar --numeric-owner --owner=0 --group=0 -T "$work/entries" -cf - | gzip -9n) > "$out"
gzip -t "$out"
echo "$out: $(wc -c < "$out") bytes, $(tar -tzf "$out" | grep -c '\.ko$') modules"
