# sc55-pack

The source of the SC-55 PACK: SBEMU with a Roland SC-55 (Nuked-SC55), MT-32 (Munt) and
SoundFont MIDI synth, the pack's DOS tools and menus, and the Windows 98 part.

- Project overview, installing, building and credits: [../README.md](../README.md)
- State of the work and what to test next: [HANDOFF.md](HANDOFF.md)
- The SC-55 additions to SBEMU and their options: [README-VSC55.md](README-VSC55.md)
- SBEMU itself (sound cards, options, memory managers): [README-SBEMU.md](README-SBEMU.md)
- The user guide that ships in the pack: [pack/README.TXT](pack/README.TXT), Windows 98: [pack/README98.TXT](pack/README98.TXT)

Build: `make -j2` with the DJGPP cross compiler on the PATH (output in `output/`).
