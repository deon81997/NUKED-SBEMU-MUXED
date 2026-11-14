D=/home/claude/mx/out/fat16-cue-rm/vm
for i in $(seq 1 ${N:-8}); do mdel -i $D/hd.img@@32256 ::SC55/SBTRACE.TXT 2>/dev/null
o=$(python3 /home/claude/mx/vmrun.py $D 203 -- CLS 'C:\T\SBIRQ' 'C:\T\SBIRQ' 'C:\T\SBIRQ' 'C:\SC55\SBDIAG.EXE FLUSH'); r=$(echo "$o" | grep -c "no IRQ")
k=$(mtype -i $D/hd.img@@32256 ::SC55/SBTRACE.TXT | tr -d '\r' | grep -c "was stuck"); echo "boot $i fails=$r kicks=$k $(echo "$o" | grep -o '[0-9]* ms to' | tr '\n' ' ')"; if [ $k != 0 ]; then mtype -i $D/hd.img@@32256 ::SC55/SBTRACE.TXT | tr -d "\r" > /home/claude/mx/trace-kick.txt; break; fi; done
