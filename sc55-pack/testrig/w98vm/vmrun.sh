#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# type a command into Start > Run in the VM
timeout 60 python3 /tmp/qk.py /tmp/w98mon.sock k:esc s:0.5 k:esc s:0.5 k:ctrl-esc s:1.5 k:r s:2.5 "t:$1" k:ret s:${2:-8}
