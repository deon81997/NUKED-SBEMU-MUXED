# Virtual CD test rig (beta.82)

- `../mkcd.py iso|cue SRCDIR OUT [LABEL] [--audio N] [--pregap] [--secs S]`: test discs from a folder (ISO 9660,
  BIN/CUE MODE1/2352 + tone tracks: L+R, L, R), plus OUT.LST (path size crc32 per file).
- `cdchk2.c` (CDCHK2.EXE, DJGPP): reads every file of the list from the drive with varying chunk sizes, findfirst
  count, random seeks in GAME\DATA\BIG.DAT (SEEK.LST). What DIR/COPY/a game's file reads do.
- `mscdx.c` (MSCDX.EXE): the MSCDEX interface like games use it: first CD drive (1500h), 150Bh/Ch/Dh, VTOC,
  1508h raw reads vs the image file, every IOCTL, READ LONG (HSG + Red Book), SEEK, PLAY + Q-channel timing,
  STOP = pause, RESUME, end of track (busy bit), PLAY on data refused. Known false "BAD" lines: 150Fh (call
  form), 1508h past the data track of the tiny TEST.CUE, the Q-channel "moves" check after exactly 1 s (74 frames).
- `cdq.c` (CDQ.EXE): prints the SBCDA1 mailbox (state, flags 01 attached/08 open/20 read/40 mixed, pos).
- `run.sh ROOT VMDIR [secs] [sed]`: 86Box P2B-LS PII-450 dynarec, DOS disk from ROOT (mkdisk), until STAGE.TXT=DONE;
  the sed adds an empty ATAPI CD drive (see HANDOFF beta.82). Real drive driver: VIDE-CDD.SYS /D:MSCD001 in CONFIG.SYS.
