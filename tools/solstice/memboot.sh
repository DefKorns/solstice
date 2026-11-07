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

host=$1
image=$2
ssh_opts="-o ConnectTimeout=6 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"

if [ -z "$host" ] || [ ! -f "$image" ]; then
  echo "usage: $0 console_host boot.img" 1>&2
  exit 1
fi

runOnConsole(){
  # shellcheck disable=SC2086,SC2029
  ssh $ssh_opts "root@$host" "$@" 2> /dev/null
}

sent=$(md5sum < "$image" | cut -d' ' -f1)
runOnConsole "mkdir -p /tmp/kexec"
runOnConsole "cat > /tmp/kexec/boot.img" < "$image"
[ "$(runOnConsole "md5sum < /tmp/kexec/boot.img" | cut -d' ' -f1)" = "$sent" ] || {
  echo "upload does not match the local image" 1>&2
  exit 1
}
runOnConsole "sntool check /tmp/kexec/boot.img" > /dev/null

runOnConsole "cd /tmp/kexec && /bin/sh /bin/detached-fallback reboot_kexec /tmp/kexec/boot.img" > /dev/null || true

sleep 20
tries=0
while [ "$tries" -lt 30 ]; do
  sleep 4
  release=$(runOnConsole "uname -r" || true)
  if [ -n "$release" ]; then
    echo "$host is up on $release"
    exit 0
  fi
  tries=$((tries + 1))
done
echo "$host did not come back" 1>&2
exit 1
