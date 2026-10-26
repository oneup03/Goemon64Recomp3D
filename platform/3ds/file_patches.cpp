// Mods' ROM patches on the 3DS (GameDesc::rom_patch, rt64-3ds). A
// translation's patch.bps is the desktop's: made against the decompressed
// ROM (32 MB, every file decompressed in turn and padded to 16 bytes), which
// this port never holds; it reads the retail ROM and decompresses each file
// as it loads it (patches/required.c). Those patches only replace bytes in
// place, inside files (the German one: 167 KB in 44 files, the text files
// and the strings of three code files), so the patch is kept as it is, with
// where each file's part of it starts, and replayed over the file once the
// game has loaded it (recomp_patch_file, before its relocations, as the
// desktop loads the patched ROM's file): the file is then as the patched ROM
// has it. A table of the changed runs would take 1.7 MB for the German
// patch's 110,000; the patch itself is 388 KB.
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include "recomp.h"
#include "librecomp/game.hpp"

namespace {

constexpr uint32_t kFileTable = 0x57FD8;    // D_800573D8_57FD8 in the ROM: each file's start, bit 31 compressed
constexpr uint32_t kCompressed = 0x80000000u;

std::vector<uint8_t> g_patch;               // the patch.bps of the mod in use
// Where each file (by table index: file id - 1) starts in the decompressed
// ROM, and where its part of the patch starts: the action that reaches it
// first and that action's output offset (pos 0: the patch leaves it alone).
std::vector<uint32_t> g_dec_start;
struct FilePart { uint32_t pos = 0, out = 0; };
std::vector<FilePart> g_parts;
uint32_t g_actions_end = 0;

void rom_read(uint32_t addr, uint8_t* dst, size_t n) {
    const std::span<const uint8_t> rom = recomp::get_rom();
    if (!rom.empty()) {
        for (size_t i = 0; i < n; i++) dst[i] = addr + i < rom.size() ? rom[addr + i] : 0;
    }
    else if (const recomp::RomStream* s = recomp::get_rom_stream()) {
        s->read(addr, dst, n);
    }
}

uint32_t rom_word(uint32_t addr) {
    uint8_t b[4];
    rom_read(addr, b, 4);
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

// The length of a file's LZKN64 data once decompressed: its commands walked
// as the desktop's lzkn64_decompress reads them (src/game/rom_decompression.cpp),
// counting what each would write.
uint32_t decompressed_length(uint32_t addr, uint32_t size) {
    std::vector<uint8_t> in(size);
    rom_read(addr, in.data(), size);
    if (size < 4) return 0;
    const uint32_t compressed = ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | in[3];
    if (compressed > size) return 0;
    uint32_t pos = 4, out = 0;
    while (pos < compressed) {
        const uint8_t c = in[pos++];
        if (c <= 0x7F) { out += ((c & 0x7C) >> 2) + 2; pos++; }    // sliding window copy
        else if (c <= 0x9F) { out += c & 0x1F; pos += c & 0x1F; }  // raw copy
        else if (c <= 0xDF) { out += (c & 0x1F) + 2; pos++; }     // run of a value
        else if (c <= 0xFE) out += (c & 0x1F) + 2;                // short run of zeros
        else { out += (uint32_t)in[pos++] + 2; }                    // long run of zeros
    }
    return out;
}

// BPS numbers (variable length, as the runtime's patcher reads them).
bool bps_number(const std::vector<uint8_t>& p, uint32_t& pos, uint64_t& value) {
    value = 0;
    uint64_t shift = 1;
    while (pos < p.size()) {
        const uint8_t x = p[pos++];
        value += (uint64_t)(x & 0x7F) * shift;
        if (x & 0x80) return true;
        shift <<= 7;
        value += shift;
    }
    return false;
}

}   // namespace

namespace goemon64 {

// GameDesc::rom_patch: takes a mod's patch.bps, if it is one this can apply.
bool use_rom_patch(const std::string& mod_id, std::span<const uint8_t> patch) {
    g_patch.assign(patch.begin(), patch.end());
    g_parts.clear();
    const auto refuse = [&](const char* why) {
        fprintf(stderr, "goemon: %s's ROM patch: %s\n", mod_id.c_str(), why);
        g_patch.clear();
        g_parts.clear();
        return false;
    };
    if (g_patch.size() < 16 || g_patch[0] != 'B' || g_patch[1] != 'P' || g_patch[2] != 'S' || g_patch[3] != '1') return refuse("not a BPS patch");
    uint32_t pos = 4;
    uint64_t source_size = 0, target_size = 0, metadata = 0;
    if (!bps_number(g_patch, pos, source_size) || !bps_number(g_patch, pos, target_size) || !bps_number(g_patch, pos, metadata)) {
        return refuse("a damaged header");
    }
    pos += (uint32_t)metadata;
    g_actions_end = (uint32_t)g_patch.size() - 12;
    if (source_size != target_size) return refuse("it changes the ROM's size");

    // The first pass: the furthest changed byte, and only in-place changes.
    uint64_t out = 0, last = 0;
    const uint32_t actions = pos;
    for (uint32_t p = actions; p < g_actions_end;) {
        uint64_t d = 0;
        if (!bps_number(g_patch, p, d)) return refuse("a damaged action");
        const uint32_t action = (uint32_t)(d & 3);
        const uint64_t length = (d >> 2) + 1;
        if (action >= 2) return refuse("it copies data about (only patches that replace bytes in place apply here)");
        if (action == 1) { p += (uint32_t)length; last = out + length; }
        out += length;
    }

    // The decompressed ROM's layout, as far as the patch reaches: each file
    // decompressed in turn from the first one's place, padded to 16 bytes
    // (goemon64::decompress_mnsg).
    g_dec_start.clear();
    uint32_t dec = rom_word(kFileTable) & ~kCompressed;
    for (uint32_t k = 0;; k++) {
        const uint32_t entry = rom_word(kFileTable + 4 * k), next = rom_word(kFileTable + 4 * k + 4);
        if (entry == 0 || next == 0) break;
        const uint32_t start = entry & ~kCompressed, size = (next & ~kCompressed) - start;
        const uint32_t length = (entry & kCompressed) ? decompressed_length(start, size) : size;
        g_dec_start.push_back(dec);
        dec += (length + 0xF) & ~0xFu;
        if (dec > last) break;
    }
    g_dec_start.push_back(dec);
    const size_t files = g_dec_start.size() - 1;
    if (files == 0 || dec < last) return refuse("it reaches past the files");

    // Where each file's part of the patch starts.
    g_parts.assign(files, FilePart{});
    out = 0;
    size_t k = 0;
    for (uint32_t p = actions; p < g_actions_end;) {
        const uint32_t at = p;
        uint64_t d = 0;
        bps_number(g_patch, p, d);
        const uint64_t length = (d >> 2) + 1;
        if ((d & 3) == 1) {
            while (k + 1 < files && g_dec_start[k + 1] <= out) k++;
            for (size_t f = k; f < files && g_dec_start[f] < out + length; f++) {
                if (g_parts[f].pos == 0) g_parts[f] = FilePart{ at, (uint32_t)out };
            }
            p += (uint32_t)length;
        }
        out += length;
    }
    size_t touched = 0;
    for (const FilePart& f : g_parts) touched += f.pos != 0;
    fprintf(stderr, "goemon: %s's ROM patch (%zu bytes): changes in %zu files, applied as each loads\n", mod_id.c_str(), g_patch.size(), touched);
    return true;
}

}   // namespace goemon64

// void recomp_patch_file(u32 file_id, u8 *dst, u32 size): the mod's changes to
// the file the game has just loaded at dst (patches/required.c).
extern "C" void recomp_patch_file(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t file_id = (uint32_t)ctx->r4, dst = (uint32_t)ctx->r5, size = (uint32_t)ctx->r6;
    if (file_id == 0 || file_id - 1 >= g_parts.size() || g_parts[file_id - 1].pos == 0) return;
    const uint32_t start = g_dec_start[file_id - 1], end = start + size;
    uint32_t p = g_parts[file_id - 1].pos;
    uint64_t out = g_parts[file_id - 1].out;
    while (p < g_actions_end && out < end) {
        uint64_t d = 0;
        bps_number(g_patch, p, d);
        const uint64_t length = (d >> 2) + 1;
        if ((d & 3) == 1) {
            // The run's bytes that fall in the file, into RDRAM (byte-swapped
            // within each word, as MEM_B addresses it).
            const uint64_t from = out > start ? out : start, to = out + length < end ? out + length : end;
            for (uint64_t o = from; o < to; o++) {
                rdram[((dst + (uint32_t)(o - start)) ^ 3) - 0x80000000u] = g_patch[p + (uint32_t)(o - out)];
            }
            p += (uint32_t)length;
        }
        out += length;
    }
}
