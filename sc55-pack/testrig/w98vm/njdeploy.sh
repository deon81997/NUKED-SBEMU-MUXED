#!/bin/sh
# copy a new NUKEDJOY.VXD into the VM's SYSTEM folder, reboot, plug the pad, log
cp /home/claude/NUKED-SBEMU-MUXED/sc55-pack/win98/joy/NUKEDJOY.VXD /home/claude/w98d/NUKEDSB/FILES/WIN98/JOY/
/tmp/vmoff.sh; /tmp/dbuild.sh; NOPAD=1 /tmp/vmboot.sh
/tmp/vmrun.sh 'command /c copy D:\NUKEDSB\FILES\WIN98\JOY\NUKEDJOY.VXD C:\WINDOWS\SYSTEM' 6
timeout 30 python3 /tmp/qk.py /tmp/w98mon.sock 'm:info status' >/dev/null
/tmp/vmoff.sh; /tmp/vmboot.sh; sleep 10
