# Goemon 64: Recompiled for the Nintendo 3DS.
#
#   cmake -S platform/3ds -B build-3ds -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=<rt64-3ds>/cmake/devkitARM-3ds.cmake
#
# The recompiled sources (RecompiledFuncs/, RecompiledPatches/, rsp/) are
# generated on the host first; see the Makefile beside this file.
cmake_minimum_required(VERSION 3.20)
project(Goemon64Recompiled3DS LANGUAGES C CXX ASM)

set(GOEMON_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(RT64_3DS_DIR "${GOEMON_ROOT}/lib/rt64-3ds" CACHE PATH "rt64-3ds checkout")
set(RT64_3DS_NMR_DIR "${GOEMON_ROOT}/lib/N64ModernRuntime")
# No mods on the 3DS (RT64_3DS_STATIC_MODS stays off: RECOMP_NO_MODS). The
# patches' RAM ends below rt64-3ds's default heap start (0x80A00000), which
# rt64_3ds_check_patch_ram below checks, so RT64_3DS_HEAP_START is not set.
# RDRAM is 12 MB instead of rt64-3ds's 16: the game's 8 MB, the patches'
# RAM (0x80801000 to about 0x808B4000) and a 2 MB recomp heap from 0x80A00000
# that nothing uses (the stats line's rheap peak is 0: no mods, no
# recomp_alloc). The 4 MB saved let an Old 3DS hold the 16 MB ROM in memory
# instead of streaming it from the SD card (main_3ds.cpp, rom_room_mb): its
# CIA's heap is about 37 MB, and the port with the ROM held takes ROM 16 +
# RDRAM + about 5 MB (the owner's console: 36-37 MB of arena with 16 MB).
set(RT64_3DS_MEM_SIZE_MB 12)
# 32-bit N64 registers (N64Recomp's recomp.h, RECOMP_GPR32): Goemon's
# recompiled code has no 64-bit register instruction, so the registers can be
# the ARM11's own width (15-25% fewer instructions in the recompiled code).
# Everything that sees recomp_context must agree, so it is set for the whole
# build; rt64_3ds_check_gpr32 below fails the configure step should the
# generated code ever gain such an instruction.
add_compile_definitions(RECOMP_GPR32=1)
add_subdirectory("${RT64_3DS_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/rt64-3ds")
rt64_3ds_check_gpr32("${GOEMON_ROOT}/RecompiledFuncs" "${GOEMON_ROOT}/RecompiledPatches")

# The generated game code. goemon_host_protos.h makes the originals weak so
# the patched functions in RecompiledPatches replace them (GCC's RECOMP_FUNC
# is not weak, clang's is). mnsg.toml's trace_mode makes every function
# include trace.h, from include/.
file(GLOB FUNC_SOURCES CONFIGURE_DEPENDS "${GOEMON_ROOT}/RecompiledFuncs/*.c")
add_library(RecompiledFuncs STATIC ${FUNC_SOURCES} "${GOEMON_ROOT}/RecompiledFuncs/lookup.cpp")
target_include_directories(RecompiledFuncs PRIVATE
    "${GOEMON_ROOT}/RecompiledFuncs" "${CMAKE_CURRENT_LIST_DIR}"
    "${RT64_3DS_NMR_DIR}/N64Recomp/include" "${RT64_3DS_NMR_DIR}/ultramodern/include" "${RT64_3DS_NMR_DIR}/librecomp/include"
    "${GOEMON_ROOT}/include")
target_compile_options(RecompiledFuncs PRIVATE
    -O2 -fno-strict-aliasing -w -Wno-implicit-function-declaration -Wno-error=implicit-function-declaration
    -include "${CMAKE_CURRENT_LIST_DIR}/goemon_host_protos.h")

add_library(RecompiledPatches STATIC
    "${GOEMON_ROOT}/RecompiledPatches/patches.c"
    "${GOEMON_ROOT}/RecompiledPatches/patches_bin.c")
target_include_directories(RecompiledPatches PRIVATE
    "${GOEMON_ROOT}/RecompiledPatches" "${CMAKE_CURRENT_LIST_DIR}"
    "${RT64_3DS_NMR_DIR}/N64Recomp/include" "${RT64_3DS_NMR_DIR}/ultramodern/include" "${RT64_3DS_NMR_DIR}/librecomp/include")
# No force-include: the patches keep the strong RECOMP_FUNC.
target_compile_options(RecompiledPatches PRIVATE
    -O2 -fno-strict-aliasing -w -Wno-implicit-function-declaration -Wno-error=implicit-function-declaration)

# The desktop's game-side files the console shares: the Controller Pak (the
# game saves there), the audio interface with the DAC's rate rounding, the
# game's file decompressor on the host (patches/required.c), stubs
# for symbols upstream librecomp lacks, the retail ROM's conversion (run on
# the first start, main_3ds.cpp) and the overlay and patch registration.
add_executable(Goemon64Recompiled3DS
    main_3ds.cpp
    recomp_api_3ds.cpp
    "${GOEMON_ROOT}/rsp/aspMain.cpp"
    "${GOEMON_ROOT}/src/game/controller_pak.cpp"
    "${GOEMON_ROOT}/src/game/audio_interface.cpp"
    "${GOEMON_ROOT}/src/game/file_decompression.cpp"
    "${GOEMON_ROOT}/src/game/unknown_symbols.cpp"
    "${GOEMON_ROOT}/src/main/register_overlays.cpp"
    "${GOEMON_ROOT}/src/main/register_patches.cpp"
)
target_include_directories(Goemon64Recompiled3DS PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}" "${GOEMON_ROOT}/include" "${GOEMON_ROOT}"
    "${GOEMON_ROOT}/RecompiledFuncs" "${GOEMON_ROOT}/RecompiledPatches"
    "${RT64_3DS_NMR_DIR}/N64Recomp/include" "${RT64_3DS_NMR_DIR}/thirdparty")
target_compile_options(Goemon64Recompiled3DS PRIVATE -O2 -Wno-unused-parameter)
# patches/transform_ids.h (the stereo rules' ids) includes PR/ultratypes.h,
# which declares nothing outside the N64 toolchain: searched last, it shadows
# none of the runtime's headers.
set_source_files_properties(main_3ds.cpp PROPERTIES
    COMPILE_OPTIONS "-idirafter;${GOEMON_ROOT}/lib/mnsg/libultra/include")
# The audio microcode runs scalar on the ARM11 (aspMain is not the naudio
# family rt64-3ds can interpret) and is the hottest file.
set_source_files_properties("${GOEMON_ROOT}/rsp/aspMain.cpp" PROPERTIES COMPILE_OPTIONS "-O3;-fno-strict-aliasing")
# Rebuild the patch registration when N64Recomp regenerates its tables.
set_source_files_properties("${GOEMON_ROOT}/src/main/register_patches.cpp" PROPERTIES
    OBJECT_DEPENDS "${GOEMON_ROOT}/RecompiledPatches/recomp_overlays.inl;${GOEMON_ROOT}/RecompiledPatches/funcs.h")
target_link_libraries(Goemon64Recompiled3DS PRIVATE
    recomp_3ds rt64_3ds RecompiledPatches RecompiledFuncs ultramodern librecomp citro3d ctru m)
target_link_options(Goemon64Recompiled3DS PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/Goemon64Recompiled3DS.map")
rt64_3ds_check_patch_ram(Goemon64Recompiled3DS "${GOEMON_ROOT}/patches/build/patches.map")
# Bring-up: accept the display lists and draw nothing.
option(GOEMON_NULL_RENDERER "Build with rt64-3ds's null renderer" OFF)
if(GOEMON_NULL_RENDERER)
    target_compile_definitions(Goemon64Recompiled3DS PRIVATE GOEMON_NULL_RENDERER)
endif()

set(GOEMON_TITLE "Goemon 64: Recompiled 3D")
# The Homebrew Launcher lists the .3dsx under a shorter title.
set(GOEMON_3DSX_TITLE "Goemon 64: Recompiled")
set(GOEMON_DESCRIPTION "Goemon 64: Recompiled 3D")
add_3dsx(Goemon64Recompiled3DS TITLE "${GOEMON_3DSX_TITLE}" DESCRIPTION "${GOEMON_DESCRIPTION}"
         ICON "${CMAKE_CURRENT_LIST_DIR}/icon.png")
include("${RT64_3DS_DIR}/cmake/Cia.cmake")
# The banner: Goemon's golden kiseru and "Ganbare! Sou ganbare!" from the
# opening song, fitted to 3 s, made by the tools in a separate repository
# (3ds-banner-tools, examples/goemon); only their output is here.
add_cia(Goemon64Recompiled3DS RSF "${RT64_3DS_DIR}/cia/template.rsf" TITLE "${GOEMON_TITLE}" PRODUCT "GOEMON-3DS" UNIQUEID 0xF564E
        ICON "${CMAKE_CURRENT_LIST_DIR}/icon.png" DESCRIPTION "${GOEMON_DESCRIPTION}"
        BANNER_CGFX "${CMAKE_CURRENT_LIST_DIR}/cia/banner.cgfx" BANNER_WAV "${CMAKE_CURRENT_LIST_DIR}/cia/banner.wav")
