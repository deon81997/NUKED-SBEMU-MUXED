# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""86Box test PC driver: start/stop the VM, type, press keys, take screenshots.
   Display :86 (Xvfb + openbox), VM in /home/claude/86rig/vm, ROMs /home/claude/86box/roms."""
import os, subprocess, time, sys
os.environ['DISPLAY'] = ':86'
# sound goes to a WAV file at real-time pace (OpenAL Soft's wave backend); no ALSA device here
os.environ['ALSOFT_CONF'] = '/home/claude/86rig/alsoft.conf'
os.environ['ALSOFT_DRIVERS'] = 'wave'
R = '/home/claude/86rig'
EXE = '/home/claude/86box/src/build/src/86Box'
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True).stdout.strip()
def win(): 
    w = sh("xdotool search --name '86Box' | tail -1"); return w
def display():
    if not sh("pgrep -x Xvfb"):
        subprocess.Popen("Xvfb :86 -screen 0 1024x768x24", shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True); time.sleep(2)
    if not sh("pgrep -x openbox"):
        subprocess.Popen("openbox", shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True); time.sleep(1)
def start(wait=30):
    display(); stop()
    subprocess.Popen(f"{EXE} -P {R}/vm -R /home/claude/86box/roms", shell=True, cwd=R+'/vm',
                     stdout=open(R+'/86box.log','w'), stderr=subprocess.STDOUT, start_new_session=True)
    time.sleep(wait); focus()
def stop():
    sh("pkill -x 86Box"); time.sleep(2)
def focus():
    w = win()
    if w: sh(f"xdotool windowactivate --sync {w} windowmove {w} 0 0")
def key(k, wait=0.3):
    focus(); sh(f"xdotool key --delay 80 {k}"); time.sleep(wait)
def type_(s, wait=0.5):
    focus()
    for line in s.split('\n'):
        if line: subprocess.run(['xdotool','type','--delay','60',line])
        if line is not s.split('\n')[-1]: sh("xdotool key Return")
    time.sleep(wait)
def cmd(c, wait=3, name=None):
    type_(c + '\n', wait)
    if name: return shot(name)
def shot(name):
    """the emulated screen only: the 86Box window minus its menu/toolbar (top 52 px) and status bar (24 px)"""
    p = f"{R}/shots/{name}.png"; w = win()
    sh(f"import -window {w} /tmp/86raw.png")
    g = sh("identify -format '%w %h' /tmp/86raw.png").split()
    W, H = int(g[0]), int(g[1])
    sh(f"convert /tmp/86raw.png -crop {W}x{H-52-24}+0+52 +repage {p}"); return p
if __name__ == '__main__':
    a = sys.argv[1:]
    {'start': lambda: start(), 'stop': stop, 'shot': lambda: print(shot(a[1]))}[a[0]]()
