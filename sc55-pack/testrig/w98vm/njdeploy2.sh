#!/bin/sh
# like njdeploy.sh, plus extra Run commands ($@) before the reboot
/tmp/vmoff.sh; /tmp/dbuild.sh; NOPAD=1 /tmp/vmboot.sh
/tmp/vmrun.sh 'command /c copy D:\NUKEDSB\FILES\WIN98\JOY\NUKEDJOY.VXD C:\WINDOWS\SYSTEM' 6
for c in "$@"; do /tmp/vmrun.sh "$c" 6; done
/tmp/vmoff.sh; /tmp/vmboot.sh; sleep 10
