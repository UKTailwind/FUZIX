#!/bin/sh
#
# Check that the things this session depends on actually landed on the
# card image.  mkccimage.sh already refuses on a ucp error and fsck's
# the result, but neither notices a file that is present and STALE, and
# a stale /usr/lib/cc/include/mmb_runtime.h is exactly what made cc
# reject the generated prologue with "type mismatch".
#
#   sh verifyimage.sh
set -e
R=$(cd "$(dirname "$0")/../../.." && pwd)
IMG=$R/Images/rpipico/pc3-sd-cc.img
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
dd if="$IMG" of="$W/p2.img" bs=512 skip=133120 count=65536 status=none

"$R/Standalone/ucp" "$W/p2.img" <<'EOF' 2>&1 | sed -n '/---/,$p'
cd /usr/bin
echo --- /usr/bin
ls
cd /usr/lib/cc
echo --- /usr/lib/cc
ls
cd /usr/lib/cc/include
echo --- mmb_runtime.h present?
ls
cd /etc
echo --- rc
cat rc
exit
EOF
