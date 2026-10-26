# Driver for the 3DS build. Host code generation first, then the cross build.
#
#   make nmr-patch    apply the 3DS runtime patch series to lib/N64ModernRuntime (once)
#   make host-tools   build N64Recomp, RSPRecomp, RecompModTool, file_to_c and mod_bake (once)
#   make codegen      fetch and bake the mods, run the recompiler over the ROM and the patches
#   make patches      the patches alone (after editing patches/*.c)
#   make              configure + build the .3dsx
#   make cia          installable title (needs makerom + bannertool)
#   make deploy-azahar / run-azahar / hw N3DS_IP=...
#
# Needs devkitPro's 3ds-dev (DEVKITPRO, default /opt/devkitpro), makerom and
# bannertool on the PATH for the CIA, clang with the MIPS target and ld.lld
# for the patches (PATCH_CC=... / LLD=... to name other ones; LLD_LIBS=...
# when that ld.lld needs its libraries on the loader path), the
# decompressed ROM in the repository root (mnsg.us.decompressed.z64, as for
# the desktop build) and the submodules, rt64-3ds (lib/rt64-3ds; RT64_3DS=...
# for a checkout elsewhere) among them. CMAKE_ARGS=... adds options to
# `configure` (-DGOEMON_NULL_RENDERER=ON for a renderer that draws nothing).
ROOT       := $(abspath ../..)
RT64_3DS   ?= $(ROOT)/lib/rt64-3ds
NMR        := $(ROOT)/lib/N64ModernRuntime
BUILD      := $(ROOT)/build-3ds
HOST       := $(ROOT)/build-host
PATCH_CC   ?= clang
LLD        ?= ld.lld
LLD_LIBS   ?=
CMAKE_ARGS ?=
TARGET     := Goemon64Recompiled3DS
MODS_OUT   := $(ROOT)/RecompiledMods
MODS_NRM   := $(ROOT)/build-mods/nrm
MODS_BUILT := $(ROOT)/build-mods/built
# Mods built into the executable (rt64-3ds tools/mod_bake): the .nrm files
# fetch_mods.sh pins and build_mods.sh builds from source, each with ":embed"
# to carry the file itself too (the German translation is a ROM patch
# alone). Changing it needs `codegen`.
BAKE_MODS  ?= $(addsuffix :embed,$(sort $(wildcard $(MODS_NRM)/*.nrm)) $(sort $(wildcard $(MODS_BUILT)/*.nrm)))
AZAHAR_SD  := $(HOME)/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc
APPDIR     := $(AZAHAR_SD)/3ds/Goemon64

# devkitPro's own pacman installs to /opt/devkitpro (3ds-dev group).
export DEVKITPRO ?= /opt/devkitpro
export DEVKITARM ?= $(DEVKITPRO)/devkitARM

all: build

# The runtime needs a small patch series for the 3DS (kept in rt64-3ds so
# every game applies the same one). Idempotent.
nmr-patch:
	$(RT64_3DS)/nmr-patches/apply.sh $(NMR)

host-tools:
	cmake -S $(NMR)/N64Recomp -B $(HOST) -G Ninja -DCMAKE_BUILD_TYPE=Release
	cmake --build $(HOST) --target N64RecompCLI RSPRecomp RecompModTool -j
	g++ -std=c++17 -O2 -o $(HOST)/file_to_c $(ROOT)/lib/rt64/src/tools/file_to_c/file_to_c.cpp
	HOST=$(HOST) OUT=$(HOST)/mod_bake $(RT64_3DS)/tools/mod_bake/build.sh $(ROOT)

# The game's and the audio microcode's recompilations are the desktop's own
# but for the baked mods: the patches' ELF comes first (mod_bake reads which
# game functions they replace), then the mods are fetched and baked, and the
# game's and the patches' recompilations run on copies of their configs with
# mod_bake's hooks appended (a dispatch check at the entry of every function
# a baked mod replaces or hooks). The desktop configs are left as they are.
codegen:
	$(MAKE) patches-elf code-mods fetch-mods
	$(MAKE) mods
	cd $(ROOT) && cat mnsg.toml $(MODS_OUT)/game_hooks.toml > mnsg.3ds.toml && $(HOST)/N64Recomp mnsg.3ds.toml
	cd $(ROOT) && $(HOST)/RSPRecomp aspMain.toml
	$(MAKE) patches-recomp

# The patches alone (after editing patches/*.c). GOEMON_3DS selects the 3DS
# variants in the patches; the desktop build leaves it undefined. They share
# patches/build and RecompiledPatches/ with the desktop build, which
# regenerates its own when it is next built.
patches: patches-elf mods patches-recomp

patches-elf:
	cd $(ROOT)/patches && $(MAKE) clean && $(if $(LLD_LIBS),LD_LIBRARY_PATH=$(LLD_LIBS)) $(MAKE) CC=$(PATCH_CC) LD=$(LLD) PLATFORM_CPPFLAGS=-DGOEMON_3DS

# The mods of mods.list, from their submodules (mods/; fetch them with
# git submodule update --init --checkout platform/3ds/mods).
code-mods:
	$(CURDIR)/build_mods.sh $(HOST)/RecompModTool $(MODS_BUILT) $(PATCH_CC) $(LLD)

# The mods' pinned releases (fetch_mods.sh).
fetch-mods:
	$(CURDIR)/fetch_mods.sh $(MODS_NRM)

mods:
	rm -rf $(MODS_OUT)
	$(HOST)/mod_bake bake $(ROOT)/Goemon64RecompSyms/mnsg.syms.toml $(ROOT)/patches/build/patches.elf $(MODS_OUT) $(BAKE_MODS)

patches-recomp:
	cd $(ROOT) && cat patches.toml $(MODS_OUT)/patch_hooks.toml > patches.3ds.toml && $(HOST)/N64Recomp patches.3ds.toml
	$(HOST)/file_to_c $(ROOT)/patches/patches.bin mm_patches_bin $(ROOT)/RecompiledPatches/patches_bin.c $(ROOT)/RecompiledPatches/patches_bin.h
	@grep -m1 -E '^ *[0-9a-f]+ +[0-9a-f]+ +[0-9a-f]+ +[0-9]+ +\.bss' $(ROOT)/patches/build/patches.map | \
	    awk '{ printf "patch .bss: 0x%s + 0x%s bytes\n", $$1, $$3 }' || true

configure:
	cmake -S . -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Release \
	    -DCMAKE_TOOLCHAIN_FILE=$(RT64_3DS)/cmake/devkitARM-3ds.cmake -DRT64_3DS_DIR=$(RT64_3DS) $(CMAKE_ARGS)

build:
	@[ -f $(BUILD)/build.ninja ] || $(MAKE) configure
	cmake --build $(BUILD) -j
	@$(DEVKITARM)/bin/arm-none-eabi-size $(BUILD)/$(TARGET) | tail -1

# The Homebrew Launcher lists a folder's .3dsx when it has the folder's name.
deploy-azahar: build
	mkdir -p $(APPDIR)
	cp -f $(BUILD)/$(TARGET).3dsx $(APPDIR)/Goemon64.3dsx
	@[ -e $(AZAHAR_SD)/3ds/dspfirm.cdc ] || : > $(AZAHAR_SD)/3ds/dspfirm.cdc
	@echo "deployed to $(APPDIR) (your ROM goes there as mnsg.us.z64)"

run-azahar: deploy-azahar
	flatpak run --env=QT_QPA_PLATFORM=xcb org.azahar_emu.Azahar -w $(APPDIR)/Goemon64.3dsx

cia: build
	cmake --build $(BUILD) --target $(TARGET)_cia
	@ls -l $(BUILD)/$(TARGET).cia

hw: build
	$(DEVKITPRO)/tools/bin/3dslink -a $(N3DS_IP) -s $(BUILD)/$(TARGET).3dsx

clean:
	rm -rf $(BUILD)

.PHONY: all nmr-patch host-tools codegen patches patches-elf code-mods fetch-mods mods patches-recomp configure build cia deploy-azahar run-azahar hw clean
