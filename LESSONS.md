# Lessons

Read this first; add 1-3 lines at the top of the right group before you stop. Keep it under 80 lines (merge, delete stale).
Hardware facts, open ideas and the user's test machines: HARVEST.md.

## Token savers
- Personal pack: public zip + store-personal files at SETUP.BAT's source paths; test with testrig/common/personal.bat (FreeDOS needs JEMMEX NOEMS for XMS, else SBEMU "Failed allocating XMS").
- run.sh needs the word PASS/FAIL as the log's last line (DONE-PF timed out); SBDIAG reports only after SBDIAG START + some MIDI (MPUTEST).
- DOS/Win98 test runs: use sc55-pack/testrig/common (restore.sh, run.sh IMAGE BATCH, shot.sh only when needed) instead of hand-driving QEMU/86Box; read the PASS/FAIL log, not screenshots.
- Restore VMs from the VM store (deon81997/priavte, store-<name> branches) instead of building them.
- Grep HANDOFF.md for your section; never read it whole. Keep command output to head/grep/tail.
- Ask the user for hardware results once, as one list; they test in batches after work.
- Write a debug/log build the user can run on real hardware (PERFLOG, SBDIAG, USBTEST, CRASH.TXT) instead of guessing; "the last line shown is the culprit" finds a hang in two runs.

## Sessions and GitHub
- Parallel chats run: git pull --rebase before every push; check the newest tag right before taking a beta number (beta.74/75 clashes happened).
- This session type cannot create repos or releases or use GraphQL (gh repo create, gh release create, gh repo view fail 403). Use REST (gh api repos/...), the tag-on-push release workflow, or the user's browser (Claude in Chrome).
- GitHub blocks creating more than ~8 repos in a few minutes ("too many repositories too quickly"); pace them.
- Spare private repos deon81997/spare-01..07 exist: rename one when a new project needs a repo and note it in HANDOFF.
- Files over 100 MB can't be committed: split into 95 MB chunks (split -b 95M -d -a 3), one orphan branch per archive, under ~1 GB per push.
- The workspace has only ~5 GB free disk: pack, push and delete one group at a time; git gc --prune=now after pushing big blobs.
- zstd is not preinstalled: apt-get install -y zstd.

## Build and edit
- A CRASHKEY write to 0xFFFFFFF0 did not fault under HDPMI; force crash tests with a null write + ud2.
- README.TXT is CRLF: detect the newline style before scripted edits (an assert failed on LF).
- After scripted C edits, compile at once: a dropped brace gave "invalid storage class" far from the edit.
- Watcom wasm rejects quoted symbol names; use plain identifiers ('@' is legal unquoted).
- Win98 CreateThread fails when lpThreadId is NULL: always pass &tid.
- On Windows hosts "python" can be the Microsoft Store alias, not Python: check with "python -V" / use py.
- HTTP tools without a User-Agent get Cloudflare error 1010 (looks like a bad API key).

## Test rigs
- MS-DOS 7 BOOTLOG "LoadFailed" for JEMMEX.EXE is false; JEMMEX after HIMEM.SYS really fails. Proof JEMM is on: JLOAD QPIEMU loads.
- Random-key soaks in a Win98 DOS box: run the program from a LOOP.BAT (Enter on an item exits it), or keys go to Explorer.
- 86Box has no EHCI: USB 2.0 paths only run in QEMU; Win98 86Box tests never load the EHCI driver.
- Never mcopy into a disk image while Windows runs on it (clusters get reused).
- MS-DOS 7.1 in QEMU reaches AUTOEXEC ~10 s after start; time events with ECHO.|TIME lines.
- A Win98 QI VM with a fresh mkdisk FAT16 D: at first boot hangs IO.SYS.
- SeaBIOS doubles grey arrow keys; JOYTEST /LOG logs -1 for a pad plugged after start in QEMU (harness quirks, not bugs).
- A rig pass is not a hardware pass: the D810 USB mouse worked in the rig and went wild on the real KBC.

## DOS, USB and hardware gotchas
- D810 keyboard controller reports idle before the byte arrives: inject PS/2 mouse bytes with a wait, or 3-byte packets slip.
- Port 60h must NOT be trapped by SBEMU (QPIEMU re-reads it).
- NEC uPD720101 OHCI can lose a TD: per-transfer ED rewrite workaround ("OHCI lost a TD" in the log) stays on.
- EHCI/UHCI/OHCI init: every hardware wait needs a timeout; check BIOS handoff on every controller (two USB 2.0 hosts froze the P5PE-VM, beta.110).
- In Windows, SweetLow's USB 2.0 stack owns the NEC EHCI: the likely cause of Win98-to-DOS USB problems; test that transition.
- JEMMEX caused "COMMAND.COM missing" on the user's Win98 install; real mode uses JEMMEX + QPIEMU only via SC55JEM.BAT.
- mpu_detect waits 0.5 s per empty port; menus must use mpu_probe.
- Never replace a user's BLASTER line; the installer keeps it and restores REM [SC55W-OLD].
- Novell ODI on the D810: NET.CFG must set frame type Ethernet_II.
- Glide on Mobility Radeon: some games crash loading levels above 640x480; drop to 640x480 first.
