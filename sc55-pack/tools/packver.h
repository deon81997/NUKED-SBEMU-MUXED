/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU */
/* packver.h - forced into every tool by the makefile (-include): the pack version, found by SBDIAG DOCTOR (BUGREP) */
#ifdef NSB_VER
static const char nsb_packver[] __attribute__((used)) = "NSBVER " NSB_VER;
#endif
