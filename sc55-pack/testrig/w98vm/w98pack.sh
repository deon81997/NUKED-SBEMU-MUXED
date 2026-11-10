#!/bin/sh
# copy the current Win98 builds onto the D: staging folder
W=/home/claude/NUKED-SBEMU-MUXED/sc55-pack/win98; P=/home/claude/w98d/NUKEDSB/FILES/WIN98
cp $W/out/SC55W.EXE $W/vxd/SC55W.VXD $P/ && cp $W/joy/NUKEDJOY.VXD $W/joy/NUKEDJOY.INF $P/JOY/
cp $W/test/WJOY.EXE $W/test/JPORT.EXE /home/claude/w98d/ 2>/dev/null
