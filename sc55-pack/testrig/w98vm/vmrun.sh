#!/bin/sh
# type a command into Start > Run in the VM
timeout 60 python3 /tmp/qk.py /tmp/w98mon.sock k:esc s:0.5 k:esc s:0.5 k:ctrl-esc s:1.5 k:r s:2.5 "t:$1" k:ret s:${2:-8}
