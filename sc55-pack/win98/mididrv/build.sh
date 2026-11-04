#!/bin/sh
set -e
cd "$(dirname "$0")"
export WATCOM=/home/claude/ow PATH=/home/claude/ow/binl64:$PATH INCLUDE=/home/claude/ow/h:/home/claude/ow/h/win
wcc -q -bt=windows -bd -ml -zu -s -ox -wx -zW -fo=sc55midi.obj sc55midi.c
wlink @sc55midi.lnk
ls -l SC55MIDI.DRV
