# USB mass storage for SBEMU: design (no code yet)

Status: research and design only, 2026-11-10. Builds on `sc55-pack/sbemu/usbpad.c` (our own polled USB host:
UHCI / OHCI / EHCI / xHCI, hubs, Xbox 360 / HID pads, boot keyboards and mice). Goal: USB sticks and card
readers as a DOS drive letter, at the same time as the pads, with no CPU cost while the drive is idle.

## 1. What is there now (and what it means for storage)

- **No USB interrupt.** `USBPAD_Poll()` runs from the sound card IRQ and from port 201h reads;
  `USBPAD_PollLight()` only collects reports. Every controller runs its own schedule all the time.
- **Pipes** (`Pipe P[MAX_PIPE=6]`) are interrupt IN endpoints only. Control transfers are synchronous, use the
  shared `TD` pool (`NTD=40`, `M_TD`) and the 512-byte `CBuf`, guarded by `Busy`.
- **Memory:** one physically contiguous XMS block `Mem` (`M_TD`, `M_CBUF`, `M_PIPE + k*0x800`,
  `M_HC + h*0x2000`), addresses through `PHYS()`. Cache flushing via `fl()` (CLFLUSH when present).
- **EHCI:** with companions, `CONFIGFLAG` is cleared, so every port (also a stick's) runs at full speed on
  UHCI / OHCI. Without companions (Intel 5-series+) EHCI is driven directly with split transactions.
- **NEC uPD720101 card** (2x OHCI + EHCI): `oslow` quirk (misses TDs appended at the tail of a running ED: new
  TDs go in by rewriting the ED while it is skipped), HCCA only read at start, slow SET_ADDRESS settle. All
  of this is already handled in enumeration and must be reused for bulk.

## 2. USB layer: bulk-only transport (BOT)

**Match:** interface class 08h, subclass 06h (SCSI transparent), protocol 50h (bulk-only). Also accept
subclass 05h/02h with protocol 50h. Not in scope: CBI (protocol 00h/01h, USB floppy drives, UFI subclass 04h)
and UAS (protocol 62h; most UAS drives also offer a BOT alternate setting, pick that one).

**New table, separate from `Pipe`:** `Msd M[MAX_MSD=2]` with dev, hc, ifc, lun count, bulk IN/OUT ep, mps,
both toggles, block size, block count, write-protect flag, `media_gen` (bumped on unplug / media change).
Pipe slots for pads are not touched, so pads and storage never compete for slots.

**Per controller:**

| HC | where bulk TDs go | packet / TD size | notes |
| --- | --- | --- | --- |
| UHCI | one bulk QH linked after the control QH (in every frame) | 64 B per TD, chain of TDs; 4 KB = 64 TDs | depth-first off, breadth bandwidth reclamation not needed |
| OHCI | bulk list (`HcBulkHeadED`, `BLE`), kick with `BLF` in `HcCommandStatus` | TD up to 8 KB (two 4 KB pages) | NEC `oslow`: build the whole chain while the ED is skipped / list off, then unskip + BLF; never append to a live tail |
| EHCI direct | a bulk QH added to the async ring next to the control QH | qTD up to 20 KB (5 pages) | HS 512 B packets; FS/LS behind the rate-matching hub: split, like the control path |
| xHCI | transfer ring per bulk endpoint (Configure Endpoint, type 2/6) | TRB per 64 KB | later session, same API |

**One transfer = CBW (31 B OUT) + data (IN or OUT) + CSW (13 B IN)**, all synchronous on the DOS caller's
thread. Max data per command limited to the bounce buffer (see 4), e.g. 64 sectors (32 KB) on EHCI, 16 KB on
UHCI/OHCI. Tag check, residue check, `bCSWStatus` 0/1/2.

**Error recovery (BOT spec 5.3.4):** STALL on data -> CLEAR_FEATURE(ENDPOINT_HALT) and read CSW; phase error
or bad CSW -> Bulk-Only Mass Storage Reset (class request FFh) + clear halt on both endpoints; reset toggles.
Two failed resets -> port reset and re-enumerate through the existing root/hub state machine.

**Enumeration additions:** GET_MAX_LUN (FEh, a STALL means 1 LUN), then per LUN: INQUIRY (skip type != 0 and
!= 0Eh), TEST UNIT READY with REQUEST SENSE loop (up to ~3 s, card readers report "not ready" for empty
slots), READ CAPACITY(10) (-> (16) if FFFFFFFFh), MODE SENSE(6) page 3Fh for the WP bit. Card readers with
several LUNs: each LUN with a medium is a drive.

## 3. SCSI layer

Commands: TEST UNIT READY, REQUEST SENSE, INQUIRY, READ CAPACITY 10/16, READ 10/WRITE 10 (READ/WRITE 16 for
> 2^32 blocks), MODE SENSE 6 (fallback 10), PREVENT ALLOW MEDIUM REMOVAL (ignore failures), SYNCHRONIZE
CACHE 10 on SBEMU unload. Sense handling: 06h/28h or 06h/29h (unit attention / reset) -> `media_gen++`,
re-read capacity, retry once; 02h/3Ah (no medium) -> drive not ready; 07h -> write protected.
Block size: 512 native; 2048/4096-byte devices are supported by splitting/merging in the block layer
(read-modify-write for writes).

## 4. Sharing with the pads, reentrancy, low CPU

- **Idle cost is zero.** No timer and no periodic bulk TDs. Storage code runs only while DOS asks for a
  sector. Media-change checks are lazy (on the next access, by TEST UNIT READY only if the last access is
  older than 2 s). Unplug is noticed by the existing root-port / hub monitoring the pads already do.
- **Who may run what:** storage requests run in the caller's context (DOS via the real-mode stub, see 5)
  with interrupts enabled, so the sound keeps playing. The sound IRQ's `USBPAD_Poll()` sees `Busy` (held
  by the storage request for the control path) or a new `MsdBusy` and drops to `PollLight()` behaviour:
  collect pad / keyboard / mouse reports, no enumeration, no control transfers. Bulk lists are separate
  from interrupt QHs/EDs, so pad reports keep flowing during a long read.
- **The other way round:** if a storage request comes in while the IRQ holds `Busy` (it cannot: DOS code does
  not run inside our IRQ), nothing to do; if a pad hot-plug enumeration is queued, it waits for the end of
  the storage request.
- **Waiting:** spin on the TD/qTD/TRB status with a TSC deadline (`us2c()`), as control transfers do. No HLT
  (traps in ring 3 under HDPMI). Short spins only: DOS is synchronous anyway.
- **Memory / DMA:** a new region in `Mem` (or a second XMS block): bulk TDs (UHCI needs ~260 TDs for 16 KB,
  16 B each; put them at `M_BULK`), CBW/CSW buffers, and a 32 KB bounce buffer. DOS buffers are copied
  into/out of the bounce buffer: no assumption that conventional memory is identity mapped (EMM386, QEMM).
- **Sector cache:** small write-through LRU (e.g. 64 KB, 128 x 512 B) in extended memory for FAT / directory
  sectors; whole-cluster read-ahead of up to 8 KB on sequential reads. Cuts USB round trips (~1 ms each on
  FS) for DOS's many small FAT reads.
- **Speed to expect:** FS (UHCI/OHCI, also EHCI companions): ~0.8-1 MB/s. EHCI HS direct: 15-30 MB/s.
- **Flag:** `USBPAD_F_DISK 4` for `USBPAD_Init`; SBEMU option (name to be chosen with the other `/U*` options)
  to switch it on. Off by default until it has had real-hardware testing.

## 5. DOS drive letter

Two front ends on one block layer (`msd_read(lun, lba, n, buf)` / `msd_write`).

### 5a. FAT12/16/32: a real DOS block device (writable)

SBEMU is loaded from AUTOEXEC as a DPMI TSR, after DOS has built its drive list, so an INT 13h hook alone is
not enough (DOS reads INT 13h drives only at boot). Do what DEVLOAD does:

1. Allocate a small resident real-mode stub in conventional memory (INT 31h 0100h, or UMB): device header
   (block device, attribute 0x0802: removable media + 32-bit sectors, + 0x0040 generic IOCTL), strategy and
   interrupt routines. The interrupt routine calls a DPMI real-mode callback (INT 31h 0303h) into SBEMU's
   protected-mode handler with the request header.
2. Handle INIT (BPB array), MEDIA CHECK (`media_gen` changed -> "changed", else "don't know" after 2 s),
   BUILD BPB, READ (4), WRITE (8/9), REMOVABLE (15), generic IOCTL 0Dh 0860h/0840h... as needed by FORMAT-less
   use, and return "drive not ready" (02h) with no medium.
3. Link it in: walk the List of Lists (INT 21h 52h), append the header to the device chain, build a DPB
   (INT 21h 53h from the BPB), link the DPB, and fill the next free CDS entry (needs `LASTDRIVE` high
   enough; report the letter, e.g. "USB disk = F:"). One DOS unit per partition (MBR primary + logical in
   extended; GPT: basic data partitions).
4. DOS does the FAT itself: FAT12/16 everywhere, FAT32 on DOS 7.1+ (Win9x DOS, FreeDOS). On MS-DOS 6.22 a
   FAT32 partition goes to the redirector (5b, read-only).
5. Unload: flush, mark the CDS entry invalid; the stub stays (DOS cannot unlink safely), returns "not ready".

### 5b. exFAT, NTFS (and FAT32 on DOS < 7.1): read-only network redirector

Hook INT 2Fh in real mode with a tiny stub that only takes AH=11h for our drive letters (checks the CDS
flags / SDA drive, chains everything else at once, so other INT 2Fh users pay nothing), and calls the PM
handler through a real-mode callback. CDS entry marked network + physical (0xC000), path `\\USB\F.`-style
as SHSUCDX / MSCDEX do. Implement: 1105 CHDIR, 1103/1104 MKDIR/RMDIR (-> 05h), 1106 CLOSE, 1107 COMMIT,
1108 READ, 1109 WRITE (-> 13h write-protect), 110C DISK SPACE, 110E SET ATTR (-> 05h), 110F GET ATTR,
1111 RENAME / 1113 DELETE (-> 05h), 1116 OPEN (read access only), 1117 CREATE (-> 05h), 111B FINDFIRST,
111C FINDNEXT, 1121 SEEK FROM END, 112E extended open (read only). File state lives in the SFT's
redirector fields (start cluster / MFT record, current run, size).

Names: DOS sees 8.3 upper-case names. exFAT: generate 8.3 aliases (`LONGNA~1.TXT`, stable per directory
order). NTFS: use the DOS-namespace `$FILE_NAME` (namespace 2) when present, else generate as for exFAT.

**exFAT reader:** boot sector (`EXFAT   `, sector/cluster shifts, FAT offset, cluster heap, root cluster),
FAT chains plus the NoFatChain (contiguous) flag of the stream extension, directory entry sets (85h file,
C0h stream, C1h names), up-case table for case-insensitive compare, ValidDataLength (bytes after it read as 0).

**NTFS reader:** boot sector (`NTFS    `, bytes/sector, sectors/cluster, MFT LCN, record size), MFT record
fixups (update sequence), attribute walk, data runs (non-resident) and resident `$DATA`, `$ATTRIBUTE_LIST`
for fragmented files, directories via `$INDEX_ROOT` + `$INDEX_ALLOCATION` (INDX records with fixups,
B+ tree walk, collation by up-case of `$UpCase`), `$MFT` itself fragmented (read its own runlist first).
Sparse runs read as zeros; compressed or encrypted `$DATA` -> access denied (05h). No journal replay:
if the volume dirty bit is set, still mount read-only and say so.

Both readers work in clusters through the sector cache; no full FAT / MFT in memory (FAT walks cached per
open file as a short run list).

## 6. Partitions

MBR (types 01, 04, 06, 0B, 0C, 0E -> FAT; 07 -> NTFS or exFAT, told apart by the boot sector OEM ID; 05/0F
extended chains), GPT behind a protective MBR (type EEh: header at LBA 1, CRC checked, basic data GUID ->
look at the boot sector). Superfloppy (no partition table, boot sector at LBA 0) is common on sticks and card
media: detect by a valid BPB / exFAT / NTFS signature at LBA 0.

## 7. Testing

- **86Box test rig:** `testrig/86box/usb_xpad.c` already emulates a pad. Add `usb_msd.c`: BOT + the SCSI
  commands above, backed by an image file, with switches for STALL / phase-error injection, unplug, and
  "not ready" LUNs (card-reader case). Images: FAT12, FAT16, FAT32, exFAT, NTFS (made on Linux with
  mkfs.* / mkntfs), MBR, GPT, superfloppy, 4K-sector.
- **USBTEST** (`tools/usbtest.c`): new screen for storage: INQUIRY / capacity, sector dump, read speed test,
  partition list. Used for real-hardware reports like the pad work.
- **Real hardware:** P5PE-VM + NEC uPD720101 card (the hard case), Dell D810 ICH6 UHCI + EHCI with companions,
  an Intel 5-series+ board (EHCI direct). Pads + keyboard + stick plugged at once, music playing while
  copying files, unplug during a read.
- **DOS versions:** MS-DOS 6.22, MS-DOS 7.1 (Win98 DOS), FreeDOS 1.3.

## 8. Work plan by session

1. **BOT core on UHCI and OHCI.** `Msd` table, interface match, bulk QH / bulk-list ED, CBW/data/CSW,
   GET_MAX_LUN, INQUIRY, READ CAPACITY, READ 10. `USBPAD_F_DISK` flag. 86Box `usb_msd.c` (read only).
   USBTEST: capacity + sector dump. Check pads still work with a stick plugged.
2. **Robustness + write.** REQUEST SENSE loop, unit attention, BOT reset recovery, WRITE 10, write-protect,
   hot-unplug (`media_gen`), multi-LUN card readers, `MsdBusy` interplay with the sound IRQ poll.
   Error injection in the test rig. Real hardware: ICH6 UHCI.
3. **NEC card + EHCI direct.** OHCI bulk with the `oslow` method on the uPD720101 (P5PE-VM), EHCI direct bulk
   (HS and split for FS sticks behind the rate-matching hub). Speed test in USBTEST.
4. **DOS block device (FAT12/16/32).** Real-mode stub + RMCB, DEVLOAD-style DPB/CDS linking, MBR/GPT/
   superfloppy, sector cache + read-ahead, media check, unload. Test on 6.22, 7.1, FreeDOS.
5. **Redirector skeleton + exFAT read-only.** INT 2Fh stub, CDS setup, the 11xxh calls, 8.3 aliases,
   exFAT reader. FAT32-on-6.22 through the same path (a small FAT reader).
6. **NTFS read-only.** MFT, runlists, index B+ trees, attribute lists, sparse; refuse compressed/encrypted.
7. **Optional / later.** EHCI HS for sticks on companion boards (set CONFIGFLAG, hand FS/LS ports to the
   companions per port with PORT_OWNER, as Linux does: changes the pad path, so needs its own regression
   pass), xHCI bulk rings, CBI/UFI USB floppy drives. Then docs, SBEMU option text, release.

## 9. Risks / open questions

- DOS internals (List of Lists, CDS, SFT, SDA layouts) differ between 6.x, 7.x and FreeDOS: use the
  documented-by-RBIL offsets per version and test all three.
- RMCB from a DOS device driver while a DPMI game is running (the game may be in PM when DOS is called
  through its own INT 21h reflection): the stub must work from V86 / real mode and must not assume SBEMU's
  PM stack is free. Use a private stack in the handler.
- HDPMI port-trap / IRQ paths (see HANDOFF.md, SSF2T investigation): do the storage handler entirely outside
  the trap code and test with the trap-heavy games.
- Long reads hold `Busy` for tens of ms on FS: hot-plug of a pad during a copy waits. Acceptable; cap one
  request at 16 KB on FS so the wait stays short.
- Some cheap sticks need a READ CAPACITY retry or a delay after SET_CONFIGURATION; keep a quirk table like
  the pad experiments on the NEC card.
