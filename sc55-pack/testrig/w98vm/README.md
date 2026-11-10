# Windows 98 SE test VM (QEMU) - helper scripts

Copies of the scripts used for the beta.80-w98.3 tests (they were in /tmp; paths inside assume
/home/claude/w98vm, /home/claude/w98d, the repo at /home/claude/NUKED-SBEMU-MUXED and QEMU at
/home/claude/src/qemu/build/qemu-system-i386 - adjust if yours differ). Copy them to /tmp to use as-is.

- run.sh       QEMU pc, P3+SSE2, 128 MB, SB16, PIIX3 UHCI; IMG=, D= (FAT16 transfer disk), CD=, EXTRA=
- vmboot.sh    boot, wait ~140 s, hot-plug the fake 360 pad (padplug.sh). NOPAD=1 skips the pad.
               The pad must come AFTER boot: with usb-redir present at boot SeaBIOS leaves IO.SYS spinning.
- vmoff.sh     Start > Shut Down and wait for QEMU to exit
- vmrun.sh     type a command into Start > Run
- qk.py        monitor helper: k:key t:text s:sleep shot:file.png m:monitor-cmd click: (tablet: device_add usb-tablet)
- dbuild.sh / dget.sh   build the D: disk from /home/claude/w98d / get a file from it
- w98pack.sh   copy current win98 builds into the D: staging folder
- njdeploy*.sh / njcheck.sh   install a new NUKEDJOY.VXD / log joysticks (WJOY) + NUKEDJOY's registry notes
- UPD.BAT (D:\) update SC55W in the VM; SETINI.BAT Tn + T*.INI (D:\) switch [Joystick] settings

Making the VM: boot the user's QuickInstall ISO (Win98 SE, qi.iso) in QEMU with an empty FAT32 partition from
testrig/mkfat32.py (`mkfat32.py build <empty dir> qi.img 1024`), spam Enter at boot (its countdown shows 0s in
QEMU), let it install and set up hardware, then boot from disk only. Snapshot the image (cp --sparse=always).
