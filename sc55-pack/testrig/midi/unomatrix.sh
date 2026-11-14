#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# unomatrix.sh [quick] - beta.114 USB MIDI matrix with the real-Uno fake (fakemidi.py default shape) on QEMU:
#   mode dos71 = MS-DOS 7.1 + HIMEM.SYS only + HDPMI32I -r (the user's Win98/DOS setup, no EMM), jem = FreeDOS + JEMMEX
#   controllers uhci (ICH-style), ohci (NEC-card style), ehcicc (ICH EHCI + UHCI companions), xhci
#   normal device: USBTEST note, SBEMU /MUSB + MIDPLAY with unplug/plug at 15 s, MIDPLAY /USB
#   hostile devices (dos71): OUT never answered, OUT always STALL, bLength 0, wTotalLength too long, no endpoints,
#   pre-firmware MidiSport 0763:1010 - every run must reach DONE (the PC kept running) and log why.
# Results: results.txt here (one PASS/FAIL line per case, last line PASS or FAIL). 2 runs at a time.
H=$(cd "$(dirname "$0")" && pwd); cd "$H" || exit 1
python3 ../midplay/genmid.py /tmp/T.MID 60 2 > /dev/null
HCS="uhci ohci ehcicc xhci"; MODES="dos71 jem"
[ "$1" = quick ] && { HCS="uhci ohci"; MODES="dos71"; }
ONLY=${ONLY:-}                                  # ONLY=regex: just the cases whose name matches
J=/tmp/unojobs.txt; : > $J; n=0
add() { [ -n "$ONLY" ] && ! [[ $1 =~ $ONLY ]] && return; n=$((n + 1)); local IFS='#'; echo "$n#$*" >> $J; }     # n#name#mode#hc#env#secs#hotplug#autoexec
for m in $MODES; do for hc in $HCS; do
  add "usbtest-$m-$hc" $m $hc "-" 60 0 'USBTEST C:\OUT.TXT /T:12 /F'
  add "sbemu-$m-$hc" $m $hc "-" 80 15 'SBEMU /VMPU /JOY:1 /MUSB:1 > C:\OUT.TXT|MIDPLAY /S /Q:25 C:\T.MID'
  add "midplay-$m-$hc" $m $hc "-" 60 0 'MIDPLAY /USB /S /Q:10 C:\T.MID > C:\OUT.TXT'
done; done
for hc in uhci ohci ehcicc; do
  add "nak-usbtest-$hc" dos71 $hc FAKEMIDI_OUT=nak 60 0 'USBTEST C:\OUT.TXT /T:12 /F'
  add "nak-sbemu-$hc" dos71 $hc FAKEMIDI_OUT=nak 80 0 'SBEMU /VMPU /JOY:1 /MUSB:1 > C:\OUT.TXT|MIDPLAY /S /Q:20 C:\T.MID'
  add "stall-midplay-$hc" dos71 $hc FAKEMIDI_OUT=stall 60 0 'MIDPLAY /USB /S /Q:10 C:\T.MID > C:\OUT.TXT'
done
for e in FAKEMIDI_BAD=zero FAKEMIDI_BAD=long FAKEMIDI_BAD=nojack FAKEMIDI_PID=1010 FAKEMIDI_OLD=1; do
  add "desc-${e#*=}-uhci" dos71 uhci $e 50 0 'USBTEST C:\OUT.TXT /T:8 /F'
done
run() {
  IFS='#' read -r i name mode hc env secs hot auto <<< "$1"
  a=(); [ $mode = dos71 ] && a+=(--dos71); [ "$hot" != 0 ] && a+=(--hotplug $hot)
  e=(); [ "$env" != - ] && e+=("$env"); rm -f done-$hc-$name.txt out-$hc-$name.txt
  [ $hc = ohci ] && [ "$env" = - ] && e+=(FAKEMIDI_PACE=0)   # QEMU's OHCI cannot take a late answer (fakemidi.py)
  env "${e[@]}" RIGTAG=-$name RIGPORT=$(( i * 5 + 100 )) timeout $((secs + 120)) python3 rig.py $hc "$auto" $secs --files /tmp/T.MID "${a[@]}" > /dev/null 2>&1
  o=out-$hc-$name.txt; f=fakemidi-$hc-$name.log
  ev=$(grep -c ' MIDI ' $f 2>/dev/null); done=no
  [ -f done-$hc-$name.txt ] && done=yes
  logs=$(cat $o usbtest-$hc-$name.txt usbmidi-$hc-$name.txt sbdetail-$hc-$name.txt 2>/dev/null)
  ok=FAIL
  case $name in
    usbtest-*) echo "$logs" | grep -q "note off sent; packets sent 2, dropped 0, errors 0" && [ "$ev" -ge 2 ] && ok=PASS ;;
    sbemu-*) [ "$ev" -ge 3000 ] && ok=PASS ;;
    midplay-*) [ "$ev" -ge 1000 ] && ok=PASS ;;
    nak-usbtest-*) [ $done = yes ] && echo "$logs" | grep -q "not taken in 0.5 s (2 in a row)" && ok=PASS ;;   # 2 transfers
    nak-sbemu-*) [ $done = yes ] && ok=PASS ;;   # SBEMU resident: MIDPLAY finished = the PC kept running

    stall-*) [ $done = yes ] && echo "$logs" | grep -q "stalled 8 times" && ok=PASS ;;
    desc-zero-*|desc-long-*|desc-nojack-*) [ $done = yes ] && ok=PASS ;;
    desc-1010-*) [ $done = yes ] && echo "$logs" | grep -q "MidiSport waiting for its firmware" && ok=PASS ;;
    desc-1-*) echo "$logs" | grep -q "note off sent; packets sent 2" && ok=PASS ;;
  esac
  # QEMU's own bug (beta.108 README): OHCI NEC mode (ED rewrite per transfer) + usbredir async OUT -> usb_cancel_packet
  [ $ok = FAIL ] && [ $hc = ohci ] && grep -q "usb_cancel_packet: Assertion" /tmp/midirig-$hc-$name/qemu.log 2>/dev/null && ok=QEMU-LIMIT
  echo "$ok $name events=$ev done=$done"
}
export -f run
: > results.txt
tr '\n' '\0' < $J | xargs -0 -P 2 -I{} bash -c 'run "$@"' _ {} | tee -a results.txt
grep -q FAIL results.txt && echo FAIL | tee -a results.txt || echo PASS | tee -a results.txt
