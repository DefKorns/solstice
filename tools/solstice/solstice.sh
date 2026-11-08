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

docker(){
  MSYS_NO_PATHCONV=1 command docker "$@"
}

here=$(cd "$(dirname "$0")" && pwd)
repo=$(git -C "$here" rev-parse --show-toplevel)
toolchain_image=${toolchain_image:-ghcr.io/defkorns/classicmini-cross-toolchain:jessie-armhf}
volume=solstice-build
sysroot=/opt/toolchain/arm-cortex_a7-linux-gnueabihf/sysroot
out=$here/output
console=
hakchi_dir=

while getopts "o:c:i:" opt; do
  case $opt in
    o) out=$OPTARG ;;
    c) console=$OPTARG ;;
    i) hakchi_dir=$OPTARG ;;
    *) echo "usage: $0 [-o output_dir] [-c console_host] [-i hakchi2ce_dir]" 1>&2; exit 1 ;;
  esac
done

if [ ! -f "$here/basehmods.tar" ]; then
  echo "put basehmods.tar from the Hakchi2-CE folder next to this script: $here" 1>&2
  exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

tar -xOf "$here/basehmods.tar" ./hakchi.hmod > "$work/hakchi.hmod" 2> /dev/null \
  || tar -xOf "$here/basehmods.tar" hakchi.hmod > "$work/hakchi.hmod"

echo "== preparing the build volume"
docker volume create "$volume" > /dev/null
git -C "$repo" -c core.autocrlf=false archive HEAD | docker run -i --rm -v "$volume:/b" "$toolchain_image" \
  sh -c 'rm -rf /b/tree /b/out && mkdir -p /b/tree /b/out && tar -x -C /b/tree'
git -C "$repo" diff HEAD --binary > "$work/changes.patch"
if [ -s "$work/changes.patch" ]; then
  docker run -i --rm -v "$volume:/b" "$toolchain_image" sh -c 'cd /b/tree && git apply -' < "$work/changes.patch"
fi
docker run -i --rm -v "$volume:/b" "$toolchain_image" sh -c 'cat > /b/base_hakchi.hmod' < "$work/hakchi.hmod"

echo "== building the kernel"
docker run --rm -v "$volume:/b" "$toolchain_image" sh -c "
  set -e
  mkdir -p /tmp/sn && tar -xzf /b/base_hakchi.hmod -C /tmp/sn bin/sntool
  printf '#!/bin/sh\nexec env QEMU_LD_PREFIX=$sysroot /tmp/sn/bin/sntool \"\$@\"\n' > /tmp/sn/sntool
  chmod +x /tmp/sn/sntool /tmp/sn/bin/sntool
  sntool_dir=/tmp/sn sh /b/tree/tools/solstice/build_kernel.sh /b/tree /b/out"

echo "== packaging"
command docker build -q -t solstice-tools "$here" > /dev/null
docker run --rm -v "$volume:/b" solstice-tools sh -c '
  set -e
  sh /b/tree/tools/solstice/make_hakchi_hmod.sh /b/base_hakchi.hmod /b/out/zImage /b/out/modules.tar /b/out/hakchi-latest.hmod
  tar -xzOf /b/out/hakchi-latest.hmod boot/boot.img > /b/out/solstice.img'

mkdir -p "$out"
docker run --rm -v "$volume:/b" "$toolchain_image" tar -C /b/out -cf - . | tar -xf - -C "$out"
echo "== output in $out"
for file in "$out"/*; do
  echo "$(wc -c < "$file") $(basename "$file")"
done

if [ -n "$console" ]; then
  echo "== booting $console from RAM"
  "$here/memboot.sh" "$console" "$out/solstice.img" "$(cat "$out/release")"
fi

if [ -n "$hakchi_dir" ]; then
  target=$hakchi_dir/data/hakchi-latest.hmod
  [ ! -f "$target" ] || cp "$target" "$target.previous"
  cp "$out/hakchi-latest.hmod" "$target"
  echo "== installed $target; run Install/Repair in Hakchi2-CE with the console in FEL mode"
fi
