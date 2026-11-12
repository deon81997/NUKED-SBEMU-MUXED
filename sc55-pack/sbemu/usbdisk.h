/* USBDISK - USB disks as DOS drive letters (FAT12/16/32 through DOS's own FAT code), docs/USB-STORAGE.md 5a.
 * A DEVLOAD-style block device: real-mode stub (usbdisk.asm) + DPMI real-mode callback into usbdisk.c, linked
 * into DOS's device chain, DPB chain and CDS. Uses only the usbpad.h disk API. GPL-2.0-or-later. */
#ifndef USBDISK_H
#define USBDISK_H

/* After USBPAD_Init(.. USBPAD_F_DISK ..). Finds the FAT partitions on every USB disk, links one DOS drive per
 * partition. Returns the number of drives linked (0: none / failed, see USBDISK_Msg). */
int USBDISK_Install(void);
/* Drive letters now, e.g. "F: G:" ("" = none); for the start-up message and SBDETAIL. */
const char* USBDISK_Letters(void);
const char* USBDISK_Msg(void);            /* what happened at install (one line) */
/* SBEMU unload: the drives answer "not ready" from then on and their CDS entries are marked invalid (the
 * stub stays: DOS cannot unlink a device safely). */
void USBDISK_Uninstall(void);

void USBDISK_RedirNote(int i, int drive, int type, const char* name);   /* usbredir.c -> USBDRV */
#endif
