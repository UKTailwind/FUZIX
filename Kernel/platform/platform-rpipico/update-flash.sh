#!/bin/sh
set -e

# Parameterised so mksdimage.sh can build the SD root from the same
# recipe: this is the only description of a working Fuzix root in the
# tree, and the SD root was otherwise an artefact nobody could rebuild.
# Defaults are the flash device; mksdimage.sh overrides all three.
#
# FS32: mkfs's second argument is an INODE COUNT, not a block count of
# inodes.  256 matches what the old "32 blocks of 8" gave the flash
# root.
IMG=${IMG:-filesystem.img}
# 8000 sectors (4M) of the 20549 mkftl can address once the disk is
# given the whole chip above FLASH_OFFSET.
#
# It was 2547 of 2555, and the root outgrew that the moment printf
# learned %ll and every static binary put on a couple of hundred bytes.
# Two things were wrong behind that: FLASH_OFFSET gave the kernel 512K
# it did not need, and PICO_FLASH_SIZE_BYTES was the pico2 default of
# 4M when a PC2 or PC3 always carries 16M - so the disk had a quarter
# of the chip and the rest was doing nothing.  See globals.h for the
# numbers that must agree.
#
# The root needs about 2600 sectors, so this is room to grow rather
# than a fit.  Inodes raised with it: 256 would have become the limit
# long before the blocks did.
FSSIZE=${FSSIZE:-8000}
INODES=${INODES:-512}

rm -f ${IMG}
../../../Standalone/mkfs ${IMG} ${INODES} $FSSIZE
../../../Standalone/ucp ${IMG} > /tmp/ucp-flash.log 2>&1 <<EOF
cd /
mkdir bin
mkdir dev
mkdir etc
mkdir mnt
mkdir root
mkdir tmp
mkdir usr
mkdir var
chmod 0755 /
chmod 0755 bin
chmod 0755 dev
chmod 0755 etc
chmod 01777 tmp
chmod 0755 usr
chmod 0755 var
chmod 0700 root

mkdir /usr/man
mkdir /usr/man/man1
mkdir /var/run

cd /usr
mkdir lib
chmod 0755 lib
# /usr/bin is empty on the flash device but MUST exist: mkccimage.sh
# cd's into it to install the compiler, and ucp's cd failing is not
# fatal - it carries on and drops every binary into whatever directory
# it was already in, which looks like a successful build.
mkdir bin
chmod 0755 bin

cd /
cd /dev
mknod tty   20666 512
mknod tty1  20660 513
mknod tty2  20660 514
mknod tty3  20660 515
mknod tty4  20660 516
mknod tty5  20660 517
mknod tty6  20660 518
mknod tty7  20660 519
mknod tty8  20660 520
mknod hda   60660 0
mknod hda1  60660 1
mknod hda2  60660 2
mknod hda3  60660 3
mknod hda4  60660 4
mknod hda5  60660 5
mknod hda6  60660 6
mknod hda7  60660 7
mknod hda8  60660 8
mknod hda9  60660 9
mknod hda10 60660 10
mknod hda11 60660 11
mknod hda12 60660 12
mknod hda13 60660 13
mknod hda14 60660 14
mknod hda15 60660 15
mknod hdb   60660 16
mknod hdb1  60660 17
mknod hdb2  60660 18
mknod hdb3  60660 19
# No hdc. The PSRAM swap disc is gone: the kernel allocates a region the
# size of the process out of the PSRAM heap and memcpy's into it, so
# there is no device to name and nothing to swapon.
mknod null  20666 1024
mknod kmem  20660 1025
mknod zero  20444 1026
mknod proc  20666 1027
mknod mem   20660 1028
mknod rtc   20600 1029
mknod sys   20644 1030
mknod i2c   20600 1031
mknod gpio  20600 1032

cd /
bget ../../../Applications/util/init init
chmod 755 init

cd /etc
bget ../../../Standalone/filesystem-src/etc-files/fstab
bget ../../../Standalone/filesystem-src/etc-files/group
bget ../../../Standalone/filesystem-src/etc-files/inittab
bget ../../../Standalone/filesystem-src/etc-files/motd
bget ../../../Standalone/filesystem-src/etc-files/mtab
bget ../../../Standalone/filesystem-src/etc-files/passwd
bget ../../../Standalone/filesystem-src/etc-files/rc
bget ../../../Standalone/filesystem-src/etc-files/rc.halt
bget ../../../Standalone/filesystem-src/etc-files/rc.reboot
bget ../../../Standalone/filesystem-src/etc-files/termcap
chmod 0644 fstab
chmod 0644 group
chmod 0644 inittab
chmod 0644 motd
chmod 0644 mtab
chmod 0644 passwd
chmod 0644 termcap
chmod 0755 rc
chmod 0755 rc.halt
chmod 0755 rc.reboot

cd /bin
bget ../../../Applications/util/banner
bget ../../../Applications/util/basename
bget ../../../Applications/util/bd
bget ../../../Applications/util/blkdiscard
bget ../../../Applications/util/cal
bget ../../../Applications/util/cat
bget ../../../Applications/util/chgrp
bget ../../../Applications/util/chmod
bget ../../../Applications/util/chown
bget ../../../Applications/util/cksum
bget ../../../Applications/util/cmp
bget ../../../Applications/util/cp
bget ../../../Applications/util/cut
bget ../../../Applications/util/date
bget ../../../Applications/util/dd
bget ../../../Applications/util/decomp16
bget ../../../Applications/util/df
bget ../../../Applications/util/dirname
bget ../../../Applications/util/dosread
bget ../../../Applications/util/du
bget ../../../Applications/util/echo
bget ../../../Applications/util/ed
bget ../../../Applications/util/env
bget ../../../Applications/util/false
bget ../../../Applications/util/fat
bget ../../../Applications/util/fdisk
bget ../../../Applications/util/fforth
bget ../../../Applications/util/fgrep
bget ../../../Applications/util/free
bget ../../../Applications/util/fsck
bget ../../../Applications/util/fsck-fuzix
bget ../../../Applications/util/grep
bget ../../../Applications/util/head
bget ../../../Applications/util/id
bget ../../../Applications/util/kill
bget ../../../Applications/util/killall
bget ../../../Applications/util/ll
bget ../../../Applications/util/logname
bget ../../../Applications/util/ls
bget ../../../Applications/util/man
bget ../../../Applications/util/marksman
bget ../../../Applications/util/mkdir
bget ../../../Applications/util/mkfs
bget ../../../Applications/util/mkfifo
bget ../../../Applications/util/mknod
bget ../../../Applications/util/more
bget ../../../Applications/util/mount
bget ../../../Applications/util/od
bget ../../../Applications/util/pagesize
bget ../../../Applications/util/passwd
bget ../../../Applications/util/printenv
bget ../../../Applications/util/prtroot
bget ../../../Applications/util/ps
bget ../../../Applications/util/pwd
bget ../../../Applications/util/reboot
bget ../../../Applications/util/remount
bget ../../../Applications/util/rm
bget ../../../Applications/util/rmdir
bget ../../../Applications/util/rx
bget ../../../Applications/util/setdate
bget ../../../Applications/util/setboot
bget ../../../Applications/util/sleep
bget ../../../Applications/util/ssh
bget ../../../Applications/util/sort
bget ../../../Applications/util/stty
bget ../../../Applications/util/substroot
bget ../../../Applications/util/sum
bget ../../../Applications/util/su
bget ../../../Applications/util/swapon
bget ../../../Applications/util/sx
bget ../../../Applications/util/sync
bget ../../../Applications/util/tar
bget ../../../Applications/util/tee
bget ../../../Applications/util/tail
bget ../../../Applications/util/telinit
bget ../../../Applications/util/touch
bget ../../../Applications/util/tr
bget ../../../Applications/util/true
bget ../../../Applications/util/umount
bget ../../../Applications/util/uniq
bget ../../../Applications/util/uptime
bget ../../../Applications/util/uud
bget ../../../Applications/util/uue
bget ../../../Applications/util/wc
bget ../../../Applications/util/which
bget ../../../Applications/util/who
bget ../../../Applications/util/whoami
bget ../../../Applications/util/write
bget ../../../Applications/util/xargs
bget ../../../Applications/util/yes
bget utils/picoctl

chmod 0755 banner
chmod 0755 basename
chmod 0755 bd
chmod 0755 blkdiscard
chmod 0755 cal
chmod 0755 cat
chmod 0755 chgrp
chmod 0755 chmod
chmod 0755 chown
chmod 0755 cksum
chmod 0755 cmp
chmod 0755 cp
chmod 0755 cut
chmod 0755 date
chmod 0755 dd
chmod 0755 decomp16
chmod 0755 df
chmod 0755 dirname
chmod 0755 dosread
chmod 0755 du
chmod 0755 echo
chmod 0755 ed
chmod 0755 env
chmod 0755 false
chmod 0755 fat
chmod 0755 fdisk
chmod 0755 fforth
chmod 0755 fgrep
chmod 0755 free
chmod 0755 fsck
chmod 0755 fsck-fuzix
chmod 0755 grep
chmod 0755 head
chmod 0755 id
chmod 0755 kill
chmod 0755 killall
chmod 0755 ll
chmod 0755 logname
chmod 0755 ls
chmod 0755 man
chmod 0755 marksman
chmod 0755 mkdir
chmod 0755 mkfs
chmod 0755 mkfifo
chmod 0755 mknod
chmod 0755 more
chmod 0755 mount
chmod 0755 od
chmod 0755 pagesize
chmod 0755 passwd
chmod 0755 printenv
chmod 0755 prtroot
chmod 0755 ps
chmod 0755 pwd
chmod 0755 reboot
chmod 0755 remount
chmod 0755 rm
chmod 0755 rmdir
chmod 0755 rx
chmod 0755 setdate
chmod 0775 setboot
chmod 0755 sleep
chmod 0755 ssh
chmod 0755 sort
chmod 0755 stty
chmod 0755 substroot
chmod 0755 sum
chmod 0755 su
chmod 0755 swapon
chmod 0755 sx
chmod 0755 sync
chmod 0755 tar
chmod 0755 tee
chmod 0755 tail
chmod 0755 telinit
chmod 0755 touch
chmod 0755 tr
chmod 0755 true
chmod 0755 umount
chmod 0755 uniq
chmod 0755 uptime
chmod 0755 uud
chmod 0755 uue
chmod 0755 wc
chmod 0755 which
chmod 0755 who
chmod 0755 whoami
chmod 0755 write
chmod 0755 xargs
chmod 0755 yes
chmod 0755 picoctl
ln cp mv
ln cp ln
ln reboot halt
ln reboot shutdown

bget ../../../Applications/V7/cmd/sh/sh
chmod 0755 sh

bget ../../../Applications/V7/cmd/ac
bget ../../../Applications/V7/cmd/accton
bget ../../../Applications/V7/cmd/at
bget ../../../Applications/V7/cmd/atrun
bget ../../../Applications/V7/cmd/col
bget ../../../Applications/V7/cmd/comm
bget ../../../Applications/V7/cmd/cron
bget ../../../Applications/V7/cmd/crypt
bget ../../../Applications/V7/cmd/dc
bget ../../../Applications/V7/cmd/dd
bget ../../../Applications/V7/cmd/deroff
bget ../../../Applications/V7/cmd/diff
bget ../../../Applications/V7/cmd/diff3
bget ../../../Applications/V7/cmd/diffh
bget ../../../Applications/V7/cmd/ed
bget ../../../Applications/V7/cmd/join
bget ../../../Applications/V7/cmd/look
bget ../../../Applications/V7/cmd/makekey
bget ../../../Applications/V7/cmd/mesg
bget ../../../Applications/V7/cmd/newgrp
bget ../../../Applications/V7/cmd/pg
bget ../../../Applications/V7/cmd/pr
bget ../../../Applications/V7/cmd/ptx
bget ../../../Applications/V7/cmd/rev
bget ../../../Applications/V7/cmd/split
bget ../../../Applications/V7/cmd/su
bget ../../../Applications/V7/cmd/sum
bget ../../../Applications/V7/cmd/test
bget ../../../Applications/V7/cmd/time
bget ../../../Applications/V7/cmd/tsort
bget ../../../Applications/V7/cmd/tty
bget ../../../Applications/V7/cmd/wall
chmod 0755 ac
chmod 0755 accton
chmod 0755 at
chmod 0755 atrun
chmod 0755 col
chmod 0755 comm
chmod 0755 cron
chmod 0755 crypt
chmod 0755 dc
chmod 0755 dd
chmod 0755 deroff
chmod 0755 diff
chmod 0755 diff3
chmod 0755 diffh
chmod 0755 ed
chmod 0755 join
chmod 0755 look
chmod 0755 makekey
chmod 0755 mesg
chmod 0755 newgrp
chmod 0755 pg
chmod 0755 pr
chmod 0755 ptx
chmod 0755 rev
chmod 0755 split
chmod 0755 su
chmod 0755 sum
chmod 0755 test
chmod 0755 time
chmod 0755 tsort
chmod 0755 tty
chmod 0755 wall

bget ../../../Applications/levee/levee
chmod 0755 levee
# levee IS the vi on this machine, and "vi" is what a hand reaches for.
# A hard link, not a second copy: same inode, one directory entry, no
# extra card space.  (Fuzix has no symbolic links.)
ln levee vi

cd /usr/man/man1
bget ../../../Applications/levee/levee.1
chmod 0644 levee.1

cd /usr/lib
bget ../../../Library/libs/liberror.txt
bget ../../../Applications/util/tchelp
chmod 0644 liberror.txt
chmod 0755 tchelp

cd /usr
mkdir games
cd /usr/games
#bget ../../../Applications/games/adv01
#bget ../../../Applications/games/adv02
#bget ../../../Applications/games/adv03
#bget ../../../Applications/games/adv04
#bget ../../../Applications/games/adv05
#bget ../../../Applications/games/adv06
#bget ../../../Applications/games/adv07
#bget ../../../Applications/games/adv08
#bget ../../../Applications/games/adv09
#bget ../../../Applications/games/adv10
#bget ../../../Applications/games/adv11
#bget ../../../Applications/games/adv12
#bget ../../../Applications/games/adv13
#bget ../../../Applications/games/adv14a
#bget ../../../Applications/games/adv14b
#bget ../../../Applications/games/advint
bget ../../../Applications/games/cowsay
#bget ../../../Applications/games/fortune
#bget ../../../Applications/games/fortune.dat
#bget ../../../Applications/games/hamurabi
#bget ../../../Applications/games/myst01
#bget ../../../Applications/games/myst02
#bget ../../../Applications/games/myst03
#bget ../../../Applications/games/myst04
#bget ../../../Applications/games/myst05
#bget ../../../Applications/games/myst06
#bget ../../../Applications/games/myst07
#bget ../../../Applications/games/myst08
#bget ../../../Applications/games/myst09
#bget ../../../Applications/games/myst10
#bget ../../../Applications/games/myst11
#bget ../../../Applications/games/qrun
#bget ../../../Applications/games/startrek
#bget ../../../Applications/games/z1
#bget ../../../Applications/games/z2
#bget ../../../Applications/games/z3
#bget ../../../Applications/games/z4
#bget ../../../Applications/games/z5
#bget ../../../Applications/games/z8
#bget ../../../Applications/cursesgames/invaders

#chmod 0755 adv01
#chmod 0755 adv02
#chmod 0755 adv03
#chmod 0755 adv04
#chmod 0755 adv05
#chmod 0755 adv06
#chmod 0755 adv07
#chmod 0755 adv08
#chmod 0755 adv09
#chmod 0755 adv10
#chmod 0755 adv11
#chmod 0755 adv12
#chmod 0755 adv13
#chmod 0755 adv14a
#chmod 0755 adv14b
#chmod 0755 advint
chmod 0755 cowsay
#chmod 0755 fortune
#chmod 0644 fortune.dat
#chmod 0755 hamurabi
#chmod 0755 myst01
#chmod 0755 myst02
#chmod 0755 myst03
#chmod 0755 myst04
#chmod 0755 myst05
#chmod 0755 myst06
#chmod 0755 myst07
#chmod 0755 myst08
#chmod 0755 myst09
#chmod 0755 myst10
#chmod 0755 myst11
#chmod 0755 qrun
#chmod 0755 startrek
#chmod 0755 z1
#chmod 0755 z2
#chmod 0755 z3
#chmod 0755 z4
#chmod 0755 z5
#chmod 0755 z8
#chmod 0755 invaders

bget ../../../Applications/cave/advent
chmod 0755 advent

cd /usr/games
mkdir lib
cd lib
bget ../../../Applications/cave/advent.db
chmod 0644 advent.db

#cd /usr/lib
#mkdir trek
#chmod 0711 trek
#cd /usr/lib/trek
#
#bget ../../../Applications/games/startrek.doc
#bget ../../../Applications/games/startrek.fatal
#bget ../../../Applications/games/startrek.intro
#bget ../../../Applications/games/startrek.logo
#chmod 0755 startrek.doc
#chmod 0755 startrek.fatal
#chmod 0755 startrek.intro
#chmod 0755 startrek.logo

EOF

# ucp exits with the status of its LAST command and does not stop on a
# failure, so a root that does not fit comes out silently short: every
# file after the one that overflowed is simply missing, fsck is happy
# because the filesystem is consistent, and the first you know is a
# missing binary on the board.  Refuse to go on instead.
#
# The ceiling is the FTL's, not the filesystem's: mkftl -s 1952 with
# 4 kB erase blocks gives 2555 sectors, and FSSIZE is set just under
# it.  Growing the root means growing the flash region.
if grep -q 'error' /tmp/ucp-flash.log; then
	echo "" >&2
	echo "***********************************************************" >&2
	echo "update-flash.sh: THE FLASH ROOT DOES NOT FIT" >&2
	echo "" >&2
	grep 'error' /tmp/ucp-flash.log | sed 's/^/    ucp: /' >&2
	echo "" >&2
	echo "  Every file after the one that overflowed is MISSING from" >&2
	echo "  filesystem.img.  fsck below will still pass - the image is" >&2
	echo "  consistent, just short - so this message is the only sign." >&2
	echo "" >&2
	echo "  FSSIZE=${FSSIZE} sectors.  The ceiling is the FTL's, not the" >&2
	echo "  filesystem's: mkftl -s 15360 with 4 kB erase blocks gives" >&2
	echo "  20549 sectors, so this is very unlikely to be the wall." >&2
	echo "" >&2
	echo "  Fix by dropping something from the list above, or by" >&2
	echo "  giving the disk more flash - that means FLASH_OFFSET in" >&2
	echo "  globals.h, mkftl's -s AND the uf2 offset in the Makefile," >&2
	echo "  all three together.  devflash.c defines the region as" >&2
	echo "  PICO_FLASH_SIZE_BYTES - FLASH_OFFSET." >&2
	echo "***********************************************************" >&2
	echo "" >&2
	# Deliberately not fatal: the SD card is the real root and this
	# image is not a release asset, so failing here would block a
	# kernel build over a fallback filesystem.  Loud instead.
fi

../../../Standalone/fsck -a ${IMG}
