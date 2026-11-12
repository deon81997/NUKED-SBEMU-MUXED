# Screenshots from the beta.96 MIDPLAY session (QEMU 8.2, std-vga, FreeDOS)

| File | What it shows |
|---|---|
| midplay3d-1024x768-32bit.png | 3D view in 1024x768, 32-bit colour |
| midplay3d-1024x768-16bit-release-build.png | the released beta.96 MIDPLAY.EXE in 1024x768, 16-bit |
| midplay3d-800x600-16bit.png | 800x600, 16-bit |
| midplay3d-800x600-8bit-dithered.png | 800x600, 256 colours (6x7x6 palette, ordered dither) |
| midplay3d-640x480-8bit-dithered.png | 640x480, 256 colours |
| midplay3d-320x200-vga-old-bar.png | VGA mode 13h 320x200 (the Windows DOS box default), before the slimmer bottom bar |
| midplay3d-320x200-vga-slim-bar.png | 320x200 with the slim bar |
| midplay3d-800x600-and-320x200-side-by-side.png | both of the above side by side |
| midplay3d-fallback-unknown-mode-to-640x480.png | MODE3D=999 777 32 (not offered) falls back to 640x480 |
| midplay-options-3d-screen-mode.png / -crop.png | Options page with "3D screen mode" set to 1024x768 256 colours |
| midplay3d-keep-this-mode-prompt-1024x768-8bit.png | the "Keep this mode? Y = yes" prompt after changing the mode |
| midplay3d-after-timeout-back-to-640x480.png | no answer: back to the last good mode (640x480) |
| qemu-arrow-key-doubled-options-rows.png | QEMU/SeaBIOS artefact: one grey arrow key moved the Options cursor two rows |
| qemu-midplay-plain-busy-synth-0m13s.png / -crop.png | MIDPLAY /S stuck at 0:13 on the slow (TCG) VM while the SoundFont synth was busy (bug fixed in beta.96) |

Screenshots taken earlier in the session (SBEMU start-up messages, the SBDIAG "cannot measure itself" box) were
overwritten by later ones of the same name and are not here.
