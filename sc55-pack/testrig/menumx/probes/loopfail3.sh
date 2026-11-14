D=/home/claude/mx/out/fat16-cue-rm/vm
for i in $(seq 1 ${N:-12}); do
o=$(python3 /home/claude/mx/vmrun.py $D 203 -- CLS 'C:\T\ESSTAT' 'C:\T\ESSTAT' 'C:\T\SBIRQ' 'C:\T\ESSTAT' 'C:\T\ESSTAT' 'C:\T\SBIRQ' 'C:\T\ESSTAT'); r=$(echo "$o" | grep -c "no IRQ")
echo "boot $i fails=$r"; echo "$o" | grep -E '^[0-9A-F]{8} ' | cut -c1-60; if [ $r != 0 ]; then break; fi; done
