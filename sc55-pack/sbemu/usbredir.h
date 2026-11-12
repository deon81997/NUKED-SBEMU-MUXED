/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* USBREDIR - exFAT / NTFS partitions on USB disks as read-only DOS drive letters: an INT 2Fh AH=11h network
 * redirector (usbredir.asm stub + DPMI real-mode callback) on top of sbemu/fsread, docs/USB-STORAGE.md 5b.
 * FAT partitions stay with USBDISK (DOS's own FAT code). Uses only the usbpad.h disk API. GPL-2.0-or-later. */
#ifndef USBREDIR_H
#define USBREDIR_H

/* After USBPAD_Init(.. USBPAD_F_DISK ..) (and after USBDISK_Install, so FAT drives keep the first letters).
 * Returns the number of drives (0: none, see USBREDIR_Msg). */
int USBREDIR_Install(void);
const char* USBREDIR_Letters(void);       /* e.g. "G: H:" */
const char* USBREDIR_Msg(void);
/* SBEMU unload: the stub chains everything from then on, the drives' CDS entries are marked invalid and INT 2Fh
 * is given back if nobody hooked it after us. */
void USBREDIR_Uninstall(void);

#endif
