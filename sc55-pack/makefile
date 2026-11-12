TARGET := output/sbemu.exe
CC := i586-pc-msdosdjgpp-gcc
CXX := i586-pc-msdosdjgpp-g++
DEBUG ?= 0

# 1 (default): MPU-401 is played by the Roland SC-55 emulation (Nuked-SC55, user supplies ROMs)
# 0: original SBEMU build with the TinySoundFont SoundFont synth
VSC55 ?= 1

VERSION ?= 1.0.0-beta.97-usb-sc55

INCLUDES := -I./mpxplay -I./sbemu -I./drivers/include -I./sc55 -I./third_party/nuked-opl3-fast
DEFINES := -D__DOS__ -DSBEMU -DDEBUG=$(DEBUG) -DSBEMU_VSC55=$(VSC55) -DMAIN_SBEMU_VER=\"$(VERSION)\"
CFLAGS := -fcommon -march=i386 -ffast-math -O2 -flto $(INCLUDES) $(DEFINES)
LDFLAGS := -lstdc++ -lm -Wno-attributes

ifeq ($(DEBUG),0)
LDFLAGS += -s
CFLAGS += -DNDEBUG
endif

ifeq ($(V),1)
SILENTCMD :=
SILENTMSG := @true
else
SILENTCMD := @
SILENTMSG := @printf
endif

VPATH += .
VPATH += sbemu
VPATH += sbemu/dpmi

SC55WAV := output/sc55wav.exe
SC55LOG := output/sc55log.exe
MPUTEST := output/mputest.exe
MIDPLAY := output/midplay.exe
CDGUI := output/cdgui.exe
SBSET := output/sbset.exe
ifeq ($(VSC55),1)
all: $(TARGET) $(SC55WAV) $(SC55LOG) $(MPUTEST) $(MIDPLAY) $(CDGUI) $(SBSET) output/sc55menu.exe output/gametest.exe output/sbdiag.exe output/timetest.exe output/joytest.exe output/usbtest.exe output/kmtest.exe output/usbdrv.exe output/sbemu16.com output/padconv.exe
else
all: $(TARGET)
endif

CARDS_SRC := mpxplay/au_cards/ac97_def.c \
	     mpxplay/au_cards/au_base.c \
	     mpxplay/au_cards/au_cards.c \
	     mpxplay/au_cards/au_linux.c \
	     mpxplay/au_cards/dmairq.c \
	     mpxplay/au_cards/pcibios.c \
	     mpxplay/au_cards/ioport.c \
	     mpxplay/au_cards/sc_cs5535.c \
	     mpxplay/au_cards/sc_e1371.c \
	     mpxplay/au_cards/sc_e1370.c \
	     mpxplay/au_cards/ak4531_def.c \
	     mpxplay/au_cards/sc_cs4281.c \
	     mpxplay/au_cards/sc_ich.c \
	     mpxplay/au_cards/sc_cmi.c \
	     mpxplay/au_cards/sc_inthd.c \
	     mpxplay/au_cards/sc_sbl24.c \
	     mpxplay/au_cards/sc_sbliv.c \
	     mpxplay/au_cards/sc_via82.c \
	     mpxplay/au_cards/sc_null.c \
	     mpxplay/au_cards/sc_ymf.c \

CTXFI_SRC := drivers/ctxfi/ctsrc.c \
             drivers/ctxfi/ctresource.c \
             drivers/ctxfi/ctmixer.c \
             drivers/ctxfi/ctimap.c \
             drivers/ctxfi/ctamixer.c \
             drivers/ctxfi/ctatc.c \
             drivers/ctxfi/cttimer.c \
             drivers/ctxfi/ctdaio.c \
             drivers/ctxfi/ctpcm.c \
             drivers/ctxfi/cthardware.c \
             drivers/ctxfi/ctvmem.c \
             drivers/ctxfi/cthw20k1.c \
             drivers/ctxfi/cthw20k2.c \
	     mpxplay/au_cards/sc_ctxfi.c \

EMU10K1_SRC := drivers/emu10k1/emu10k1x.c \
	       mpxplay/au_cards/sc_emu10k1x.c \

TRIDENT_SRC := drivers/trident/trident_main.c \
               drivers/trident/trident_memory.c \
	       mpxplay/au_cards/sc_trident.c \

ALS4000_SRC := drivers/als4000/als4000.c \
	       drivers/als4000/sb_common.c \
	       drivers/als4000/sb_mixer.c \
	       mpxplay/au_cards/sc_als4000.c \

OXYGEN_SRC := drivers/oxygen/xonar_dg.c \
	      drivers/oxygen/xonar_dg_mixer.c \
	      drivers/oxygen/xonar_lib.c \
	      drivers/oxygen/oxygen.c \
	      drivers/oxygen/oxygen_io.c \
	      drivers/oxygen/oxygen_lib.c \
	      drivers/oxygen/oxygen_pcm.c \
	      drivers/oxygen/oxygen_mixer.c \
	      mpxplay/au_cards/sc_oxygen.c \

ALLEGRO_SRC := drivers/maestro3/maestro3.c \
	       mpxplay/au_cards/sc_allegro.c \

SBEMU_SRC := sbemu/dbopl.cpp \
	     sbemu/opl3emu.cpp \
	     sbemu/pic.c \
	     sbemu/sbemu.c \
	     sbemu/untrapio.c \
	     sbemu/vdma.c \
	     sbemu/virq.c \
	     sbemu/serial.c \
             sbemu/vpcspeaker.c \
             sbemu/usbpad.c \
             sbemu/vjoy.c \
             sbemu/vkbc.c \
             sbemu/padmap.c \
             sbemu/padconvl.c \
	     sbemu/dpmi/xms.c \
	     sbemu/dpmi/dpmi.c \
	     sbemu/dpmi/dbgutil.c \
	     sbemu/dpmi/dpmi_dj2.c \
	     sbemu/dpmi/dpmi_tsr.c \
	     sbemu/dpmi/djgpp/gormcb.c \
		 sbemu/dpmi/djgpp/gopint.c \
	     main.c \
	     sbemu/cdlink.c \
	     sbemu/usbdisk.c \
	     sbemu/usbredir.c sbemu/fsread/exfat.c sbemu/fsread/ntfs.c \
	     qemm.c \
	     utility.c \
	     hdpmipt.c \
		 irqguard.c \

ifeq ($(VSC55),1)
SBEMU_SRC += sbemu/vmpu_sc55.c sbemu/vmpu_sf.c sbemu/vdiag.c
else
SBEMU_SRC += sbemu/vmpu.c
endif

# ---- Roland SC-55 emulation (Nuked-SC55 0.7.0, compiled unchanged) -------------------
# Own flags: no -ffast-math (the emulation must be bit-exact), i686 baseline, C++20, and
# shims for what DJGPP's GCC 12 lacks (std::mutex, std::unreachable); see sc55/shim.
CORE_DIR := upstream/Nuked-SC55/src/backend
CORE_CXXFLAGS := -std=gnu++20 -O3 -march=i686 -mtune=pentium-m -fomit-frame-pointer -fno-rtti -DNDEBUG \
	-I./sc55/shim -I./sc55/config -I./sc55 -I./$(CORE_DIR) -include ./sc55/shim/sc55_compat.h
CORE_CFLAGS := -O3 -march=i686 -mtune=pentium-m -fomit-frame-pointer -DNDEBUG
# Profile-guided build of the SC-55 core (bit-exact, ~8-10% faster): the profiles in sc55/pgo come from an instrumented
# SC55WAV run in DOS (testrig/sc55bench/pgo.sh). SC55PGO=gen builds the instrumented core, =use (default when the
# profiles are there) builds with them, =off without. A changed source file just falls back to the normal build.
SC55PGO ?= $(if $(wildcard sc55/pgo/*.gcda),use,off)
ifeq ($(SC55PGO),gen)
CORE_CXXFLAGS += -fprofile-generate -fprofile-update=single
endif
ifeq ($(SC55PGO),use)
CORE_CXXFLAGS += -fprofile-use=$(CURDIR)/sc55/pgo -fprofile-prefix-path=$(CURDIR) -fprofile-correction -fprofile-partial-training -Wno-missing-profile -Wno-coverage-mismatch
endif
# Nuked OPL3 runs for every game (SB FM), so it must run on any Pentium-class CPU: i586 (no CMOV).
OPL_CFLAGS := -O3 -march=i586 -mtune=pentium-m -fomit-frame-pointer -DNDEBUG
SC55_SRC :=
SC55_OBJS :=
ifeq ($(VSC55),1)
CORE_SRC := $(wildcard $(CORE_DIR)/*.cpp)
CORE_OBJS := $(patsubst %.cpp,output/%.o,$(CORE_SRC))
SHA_OBJ := output/$(CORE_DIR)/sha/sha224-256.o
SC55_OBJS := $(CORE_OBJS) $(SHA_OBJ) output/sc55/sc55synth.o
endif

# ---- Roland MT-32 / CM-32L emulation (Munt libmt32emu 2.8.3, LGPL-2.1+, compiled unchanged) -------
MT32_DIR := third_party/mt32emu
MT32_SRC := Analog BReverbModel Display File FileStream LA32FloatWaveGenerator LA32Ramp LA32WaveGenerator \
	MidiStreamParser Part Partial PartialManager Poly ROMInfo Synth Tables TVA TVF TVP sha1/sha1 SampleRateConverter \
	srchelper/srctools/src/FIRResampler srchelper/srctools/src/SincResampler srchelper/srctools/src/IIR2xResampler \
	srchelper/srctools/src/LinearResampler srchelper/srctools/src/ResamplerModel srchelper/InternalResampler
MT32_CXXFLAGS := -std=gnu++11 -O2 -march=i686 -mtune=pentium-m -fomit-frame-pointer -DNDEBUG \
	-DMT32EMU_WITH_INTERNAL_RESAMPLER -DMT32EMU_WITH_STD_SNPRINTF -I./$(MT32_DIR) \
	-include ./sbemu/mt32_silent.h -fno-builtin-printf
MT32_OBJS :=
ifeq ($(VSC55),1)
MT32_OBJS := $(patsubst %,output/$(MT32_DIR)/%.o,$(MT32_SRC)) output/sbemu/vmpu_mt32.o
endif
SC55_OBJS += $(MT32_OBJS)

LINUX_DRIVERS_SRC := $(CTXFI_SRC) $(EMU10K1_SRC) $(TRIDENT_SRC) $(ALS4000_SRC) $(OXYGEN_SRC) $(ALLEGRO_SRC)
SRC := $(LINUX_DRIVERS_SRC) $(CARDS_SRC) $(SBEMU_SRC)
NUKEDOPL_OBJ := output/third_party/nuked-opl3-fast/opl3.o
OBJS := $(patsubst %.cpp,output/%.o,$(patsubst %.c,output/%.o,$(SRC))) $(SC55_OBJS) $(NUKEDOPL_OBJ)
# no per-file header tracking: rebuild every object when any project header changes
# (a stale object built against an older opl3.h corrupted memory in beta.50 testing)
PROJECT_HEADERS := $(wildcard sbemu/*.h sbemu/*/*.h mpxplay/*.h mpxplay/au_cards/*.h sc55/*.h third_party/*/*.h $(CORE_DIR)/*.h)
$(OBJS): $(PROJECT_HEADERS)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(SILENTMSG) "LINK\t$@\n"
	$(SILENTCMD)$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Nuked-OPL3-fast (VOPL3): compiled without -ffast-math, bit-exact by design
$(NUKEDOPL_OBJ): output/%.o: %.c
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) $(OPL_CFLAGS) -I./third_party/nuked-opl3-fast -c $< -o $@

ifeq ($(VSC55),1)
output/tools/sc55wav.o: tools/sc55wav.cpp sc55/sc55synth.h
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CXX\t$@\n"
	$(SILENTCMD)$(CXX) $(CORE_CXXFLAGS) -I./sbemu -I./third_party/nuked-opl3-fast -c $< -o $@

$(MPUTEST): tools/mputest.c $(wildcard tools/*.h)
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) -O2 -Wall -o $@ $< $(if $(filter 0,$(DEBUG)),-s,)

output/gametest.exe: tools/gametest.c $(wildcard tools/*.h)
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) -O2 -Wall -march=i586 -o $@ $< -lm $(if $(filter 0,$(DEBUG)),-s,)

output/sbdiag.exe: tools/sbdiag.c $(wildcard tools/*.h)
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) -O2 -Wall -march=i586 -o $@ $< $(if $(filter 0,$(DEBUG)),-s,)

USBTEST_SRC := tools/usbtest.c sbemu/usbpad.c sbemu/padmap.c sbemu/padconvl.c mpxplay/au_cards/pcibios.c mpxplay/au_cards/au_base.c \
	sbemu/dpmi/dpmi.c sbemu/dpmi/dpmi_dj2.c sbemu/dpmi/xms.c sbemu/dpmi/dbgutil.c sbemu/dpmi/dpmi_tsr.c \
	sbemu/pic.c sbemu/serial.c sbemu/untrapio.c
output/usbtest.exe: $(USBTEST_SRC) sbemu/usbpad.h sbemu/padmap.h $(wildcard tools/*.h)
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) -O2 -march=i386 -fcommon -D__DOS__ -DSBEMU -DDEBUG=0 -DNDEBUG -I./mpxplay -I./sbemu -I./drivers/include -o $@ $(USBTEST_SRC) -s

output/padconv.exe: tools/padmap/padconv.c
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) -O2 -march=i386 -o $@ $< -s

# USB disk block device stub (bytes in a header; the header is committed, nasm only needed after an .asm change)
sbemu/usbdiskbin.h: sbemu/usbdisk.asm
	$(SILENTMSG) "NASM\t$@\n"
	$(SILENTCMD)nasm -f bin -o output/usbdisk.bin $< && (echo "/* generated from usbdisk.asm (makefile) */"; \
	  echo "static const unsigned char USBDISK_Stub[] = {"; od -An -v -tx1 output/usbdisk.bin | sed 's/ \([0-9a-f][0-9a-f]\)/0x\1,/g'; echo "};") > $@
output/sbemu/usbdisk.o: sbemu/usbdiskbin.h
output/sbemu/usbredir.o: sbemu/usbredirbin.h

output/sbemu16.com: tools/sbemu16.asm
	@mkdir -p $(dir $@)
	$(SILENTMSG) "NASM\t$@\n"
	$(SILENTCMD)nasm -f bin -o $@ $<

output/usbdrv.exe: tools/usbdrv.c $(wildcard tools/*.h)
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) -O2 -Wall -march=i386 -o $@ $< $(if $(filter 0,$(DEBUG)),-s,)

output/kmtest.exe: tools/kmtest.c $(wildcard tools/*.h)
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) -O2 -Wall -march=i586 -o $@ $< $(if $(filter 0,$(DEBUG)),-s,)

output/joytest.exe: tools/joytest.c $(wildcard tools/*.h)
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) -O2 -Wall -march=i586 -o $@ $< $(if $(filter 0,$(DEBUG)),-s,)

output/timetest.exe: tools/timetest.c $(wildcard tools/*.h)
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) -O2 -Wall -march=i586 -o $@ $< $(if $(filter 0,$(DEBUG)),-s,)

$(MIDPLAY): tools/midplay.c $(wildcard tools/*.h)
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) -O2 -Wall -march=i586 -o $@ $< -lm $(if $(filter 0,$(DEBUG)),-s,)

output/tools/sbset.o: tools/sbset.c sc55/sc55synth.h $(wildcard tools/*.h)
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) -O2 -Wall -march=i586 -c $< -o $@

$(SBSET): output/tools/sbset.o $(SC55_OBJS) $(NUKEDOPL_OBJ) output/sbemu/dbopl.o
	@mkdir -p $(dir $@)
	$(SILENTMSG) "LINK\t$@\n"
	$(SILENTCMD)$(CXX) -O2 -o $@ $^ -lstdc++ -lm $(if $(filter 0,$(DEBUG)),-s,)

SC55MENU := output/sc55menu.exe
$(SC55MENU): tools/sc55menu.c $(wildcard tools/*.h)
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) -O2 -Wall -march=i586 -ffast-math -DMENU_VER=\"$(VERSION)\" -o $@ $< -lm $(if $(filter 0,$(DEBUG)),-s,)

$(CDGUI): tools/cdgui.c $(wildcard tools/*.h)
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) -O2 -Wall -march=i586 -o $@ $< -lm $(if $(filter 0,$(DEBUG)),-s,)

# same tool, but it also writes the whole report to SC55LOG.TXT next to the exe
output/tools/sc55log.o: tools/sc55wav.cpp sc55/sc55synth.h
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CXX\t$@\n"
	$(SILENTCMD)$(CXX) $(CORE_CXXFLAGS) -DSC55LOG -I./sbemu -I./third_party/nuked-opl3-fast -c $< -o $@

$(SC55LOG): output/tools/sc55log.o $(SC55_OBJS) $(NUKEDOPL_OBJ) output/sbemu/dbopl.o
	@mkdir -p $(dir $@)
	$(SILENTMSG) "LINK\t$@\n"
	$(SILENTCMD)$(CXX) -O2 -o $@ $^ -lstdc++ -lm $(if $(filter 0,$(DEBUG)),-s,)

$(SC55WAV): output/tools/sc55wav.o $(SC55_OBJS) $(NUKEDOPL_OBJ) output/sbemu/dbopl.o
	@mkdir -p $(dir $@)
	$(SILENTMSG) "LINK\t$@\n"
	$(SILENTCMD)$(CXX) -O2 -o $@ $^ -lstdc++ -lm $(if $(filter 0,$(DEBUG)),-s,)

output/$(MT32_DIR)/%.o: $(MT32_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CXX\t$@\n"
	$(SILENTCMD)$(CXX) $(MT32_CXXFLAGS) -c $< -o $@

output/sbemu/vmpu_mt32.o: sbemu/vmpu_mt32.cpp sbemu/vmpu_mt32.h
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CXX\t$@\n"
	$(SILENTCMD)$(CXX) $(MT32_CXXFLAGS) -c $< -o $@

$(CORE_OBJS): output/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CXX\t$@\n"
	$(SILENTCMD)$(CXX) $(CORE_CXXFLAGS) -c $< -o $@

output/sc55/sc55synth.o: sc55/sc55synth.cpp sc55/sc55engine.hpp sc55/sc55synth.h
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CXX\t$@\n"
	$(SILENTCMD)$(CXX) $(CORE_CXXFLAGS) -c $< -o $@

$(SHA_OBJ): output/%.o: %.c
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) $(CORE_CFLAGS) -c $< -o $@
endif

output/%.o: %.c
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) $(CFLAGS) -c $< -o $@

output/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(SILENTCMD)$(SILENTMSG) "CXX\t$@\n"
	$(SILENTCMD)$(CXX) $(CFLAGS) -c $< -o $@

clean:
	$(SILENTMSG) "CLEAN\n"
	$(SILENTCMD)$(RM) $(OBJS)
# delete LTO -save-temps files
	$(SILENTCMD)$(RM) output/sbemu.ltrans*

distclean: clean
	$(SILENTMSG) "DISTCLEAN\n"
	$(SILENTCMD)$(RM) $(TARGET)
