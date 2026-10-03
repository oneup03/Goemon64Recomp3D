// Controller Pak emulation: Goemon saves to the Controller Pak, and upstream
// librecomp's pak.cpp reports that no pak is inserted. This presents one pak
// holding one file, Goemon's 0x1000-byte save, backed by the runtime's save
// buffer (librecomp's save_read/save_write, defined in its pi.cpp), as the
// goemon-patches fork of N64ModernRuntime did inside the runtime.
//
// librecomp is a static library, and this file defines every symbol that its
// pak.cpp object exports (the ten osPfs* functions there), plus osPfsInit and
// osPfsIsPlug, which it lacks. The linker resolves the recompiled code's
// references to this executable's definitions first, so it never extracts
// librecomp's pak.o from the archive and there is no duplicate definition.
// Keep the two sets equal: should upstream add a symbol to pak.cpp, define it
// here too, or the archive member would be pulled in and clash.

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

#include "recomp.h"
#include "librecomp/helpers.hpp"

// Defined in librecomp's pi.cpp with external linkage, not declared in any of
// its headers.
void save_write(RDRAM_ARG PTR(void) rdram_address, uint32_t offset, uint32_t count);
void save_read(RDRAM_ARG PTR(void) rdram_address, uint32_t offset, uint32_t count);

namespace {
    constexpr s32 PFS_OK = 0;
    constexpr s32 PFS_ERR_INVALID = 5;

    // The one file on the pak: Goemon's save.
    constexpr u32 pak_file_size = 0x1000;
    constexpr u32 pak_game_code = 0x4E473545; // "NG5E"
    constexpr u16 pak_company_code = 0x4A;
}

extern "C" void osPfsInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_OK;
}

extern "C" void osPfsInitPak_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_OK;
}

extern "C" void osPfsFreeBlocks_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_OK;
}

extern "C" void osPfsAllocateFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_OK;
}

extern "C" void osPfsDeleteFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_INVALID;
}

extern "C" void osPfsFileState_recomp(uint8_t* rdram, recomp_context* ctx) {
    s32 file_no = _arg<1, s32>(rdram, ctx);
    PTR(void) state = _arg<2, PTR(void)>(rdram, ctx);

    // Only the first file is valid.
    if (file_no == 0) {
        MEM_W(0, state) = pak_file_size; // file_size
        MEM_W(4, state) = pak_game_code; // game_code
        MEM_H(8, state) = pak_company_code; // company_code

        for (s32 i = 0; i < 4; i++) {
            MEM_B(10 + i, state) = 0; // ext_name
        }

        for (s32 i = 0; i < 16; i++) {
            MEM_B(14 + i, state) = 0; // game_name
        }

        ctx->r2 = PFS_OK;
    } else {
        ctx->r2 = PFS_ERR_INVALID;
    }
}

extern "C" void osPfsFindFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    // The game's file is the first one. The fork zeroed the sixth argument's
    // stack slot (the file_no pointer itself); this stores through it, as
    // libultra does, so the file_no the game later passes to
    // osPfsReadWriteFile and osPfsFileState (read back from the same variable)
    // is 0 whatever the variable held before.
    PTR(s32) file_no = _arg<5, PTR(s32)>(rdram, ctx);
    if (file_no != 0) {
        MEM_W(0, file_no) = 0;
    }
    ctx->r2 = PFS_OK;
}

extern "C" void osPfsReadWriteFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    s32 file_no = _arg<1, s32>(rdram, ctx);
    u8 flag = _arg<2, u8>(rdram, ctx);
    s32 offset = _arg<3, s32>(rdram, ctx);
    s32 size_in_bytes = MEM_W(16, ctx->r29);
    PTR(u8) data = MEM_W(20, ctx->r29);

    // Only the first file exists.
    if (file_no != 0 || offset < 0 || size_in_bytes < 0) {
        ctx->r2 = PFS_ERR_INVALID;
        return;
    }

    if (flag == 0) { // PFS_READ
        save_read(rdram, data, offset, size_in_bytes);
    } else if (flag == 1) { // PFS_WRITE
        save_write(rdram, data, offset, size_in_bytes);
    }

    ctx->r2 = PFS_OK;
}

extern "C" void osPfsChecker_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_OK;
}

extern "C" void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Bit pattern of the controllers with a pak inserted: the first one.
    MEM_B(0, ctx->r5) = 1;
    ctx->r2 = PFS_OK;
}

extern "C" void osPfsNumFiles_recomp(uint8_t* rdram, recomp_context* ctx) {
    s32* max_files = _arg<1, s32*>(rdram, ctx);
    s32* files_used = _arg<2, s32*>(rdram, ctx);

    *max_files = 1;
    *files_used = 1;

    _return<s32>(ctx, PFS_OK);
}

extern "C" void osPfsRepairId_recomp(uint8_t* rdram, recomp_context* ctx) {
    _return<s32>(ctx, PFS_OK);
}
