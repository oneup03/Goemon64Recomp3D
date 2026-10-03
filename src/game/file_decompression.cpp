// The game's file decompressor (func_80005394_5F94, LZKN64) on the host, for
// patches/required.c's RECOMP_PATCH of it. The game reads its compressed
// files from the retail ROM and decompresses them as it loads them; recompiled,
// that is a function call and a dozen RAM accesses for every input byte, a
// seventh of a second for the largest file on the 3DS. This does the same work
// natively, step for step: the same 0x2000-byte reads from the ROM into the
// game's input ring (its work area + 0xC5FA0), each waited for on the game's
// DMA queue as the original does (where the other threads get their turn),
// the same output, the same state variables left behind and the same return
// value (the output's end), so the game cannot tell the difference. It was
// checked against the recompiled original on every compressed file of the
// retail ROM and on generated input, comparing all of RDRAM afterwards.
//
// The one liberty: the original keeps its state in RAM and rereads it at every
// byte, which only matters if a file were decompressed over that state
// (0x801673A0..0x801673B8); none is.
//
// Shared by the desktop and the 3DS builds (both list this file).
#include <cstdint>

#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "librecomp/game.hpp"
#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

namespace {

// Where the game keeps the decompressor's state (KSEG0 addresses).
constexpr uint32_t kWorkBase = 0x8015C5C8;  // D_8015C5C8: the work area; the input ring is at +kRingOffset
constexpr uint32_t kRingOffset = 0xC5FA0;
constexpr uint32_t kRingSize = 0x2000;
constexpr uint32_t kDmaRom = 0x801673A0;    // ROM address of the next block to read
constexpr uint32_t kPos = 0x801673A4;       // ROM address of the next input byte
constexpr uint32_t kDmaLeft = 0x801673A8;   // bytes still to read from the ROM
constexpr uint32_t kLeft = 0x801673AC;      // input bytes left, less one
constexpr uint32_t kRingLeft = 0x801673B0;  // bytes left in the ring
constexpr uint32_t kStream = 0x801673B4;    // u16: the stream being written (see run)
constexpr uint32_t kStride = 0x801673B6;    // u16: the number of streams, the output's stride
constexpr uint32_t kDmaQueue = 0x8008A868;  // D_8008A868: the queue func_80001640_2240's DMAs reply to

inline gpr kseg0(uint32_t addr) { return (gpr)(int32_t)addr; }

struct Lzkn64 {
    uint8_t* rdram;
    // The state variables, kept here while running and stored at the end.
    // 32-bit wrapping arithmetic, as on the MIPS.
    uint32_t dma_rom, pos, dma_left, left, ring_left;
    uint32_t in;    // the input byte's address in the ring
    uint32_t out;   // the next output byte's address

    // RDRAM is byte-swapped within each word, as MEM_B/MEM_BU address it.
    uint8_t& byte(uint32_t addr) { return rdram[(addr ^ 3) - 0x80000000u]; }
    uint16_t half(uint32_t addr) { return (uint16_t)MEM_HU(0, kseg0(addr)); }
    void set_half(uint32_t addr, uint32_t v) { MEM_H(0, kseg0(addr)) = (int16_t)v; }
    void set_word(uint32_t addr, uint32_t v) { MEM_W(0, kseg0(addr)) = (int32_t)v; }
    uint32_t ring() { return (uint32_t)MEM_W(0, kseg0(kWorkBase)) + kRingOffset; }

    // func_80005984_6584: the next block of input into the ring, through
    // func_80001640_2240: osPiStartDma (the runtime's copies at once and
    // posts the completion to the queue) and a blocking osRecvMesg for it.
    // Its cache invalidations are no-ops in the recompilation, its closing
    // busy-wait (0x1000 turns of a loop) is left out.
    void refill() {
        uint32_t n = kRingSize;
        ring_left = kRingSize;
        dma_left -= kRingSize;
        if ((int32_t)dma_left < 0) {
            n = kRingSize + ((dma_left + 1) & ~1u);
            ring_left = n;
            dma_left = 0;
        }
        if ((int32_t)n > 0) {
            recomp::do_rom_read(rdram, kseg0(ring()), (dma_rom | recomp::rom_base) & 0x1FFFFFFF, n);
            ultramodern::enqueue_external_message_src((PTR(OSMesgQueue))kDmaQueue, 0, false, ultramodern::EventMessageSource::Pi);
            osRecvMesg(rdram, (PTR(OSMesgQueue))kDmaQueue, NULLPTR, OS_MESG_BLOCK);
        }
        dma_rom += n;
    }

    // func_80005900_6500: step past the current input byte.
    void advance() {
        pos++;
        ring_left--;
        left--;
        if (ring_left == 0) {
            in = ring();
            refill();
        }
        else {
            in++;
        }
    }

    uint8_t peek() { return byte(in); }
    uint8_t read() { uint8_t b = peek(); advance(); return b; }

    uint32_t header(uint8_t first) {
        uint32_t h = (uint32_t)first << 24;
        advance();
        h += (uint32_t)read() << 16;
        h += (uint32_t)read() << 8;
        h += read();
        return h;
    }

    // One chunk's commands until the input reaches `end`, writing a byte
    // every `stride` bytes.
    void commands(uint32_t end, uint32_t stride) {
        while (pos < end) {
            const uint8_t cmd = read();
            uint32_t n;
            uint8_t value = 0;
            if (cmd < 0x80) {
                // Copy 2..33 bytes from up to 0x3FF strides back, a byte at a
                // time (the source may overlap what it writes).
                const uint32_t back = (((uint32_t)cmd << 8) + read()) & 0x3FF;
                uint32_t src = out - back * stride;
                for (n = ((cmd >> 2) + 2) & 0xFFFF; n != 0; n--) {
                    byte(out) = byte(src);
                    out += stride;
                    src += stride;
                }
                continue;
            }
            if (cmd < 0xA0) {
                // 0..31 literal bytes.
                for (n = cmd & 0x1F; n != 0; n--) {
                    byte(out) = peek();
                    out += stride;
                    advance();
                }
                continue;
            }
            if (cmd < 0xE0) {
                n = (cmd & 0x1F) + 2;       // a byte 2..33 times
                value = read();
            }
            else if (cmd < 0xFF) {
                n = (cmd & 0x1F) + 2;       // zeros 2..32 times
            }
            else {
                n = read() + 2;             // zeros 2..257 times
            }
            for (; n != 0; n--) {
                byte(out) = value;
                out += stride;
            }
        }
    }

    // func_80005394_5F94(rom_addr, dst, size): returns the output's end.
    uint32_t run(uint32_t rom_addr, uint32_t dst, uint32_t size) {
        pos = rom_addr;
        dma_rom = rom_addr;
        dma_left = size;
        left = size - 1;
        in = ring();
        out = dst;
        refill();
        if ((int32_t)left > 0) {
            uint8_t first = peek();
            for (;;) {
                // A chunk: its length (header included, big-endian).
                uint32_t h = header(first);
                uint32_t end = pos + h - 4;
                if ((h & 0xF0000000) != 0) {
                    // Never in the retail ROM: the top nibble stays in the
                    // end (as in the original), so the loop runs only if it
                    // wraps below the position.
                    commands(end, 1);
                }
                else {
                    // Written as (top nibble + 1) streams interleaved byte by
                    // byte, which with the nibble 0 here is always one.
                    const uint32_t streams = (h >> 28) + 1;
                    set_half(kStream, 0);
                    set_half(kStride, streams);
                    uint32_t base = out;
                    uint32_t index = 0;
                    if ((int32_t)index < (int32_t)(streams & 0xFFFF)) {
                        for (;;) {
                            if (index != 0) {
                                h = header(peek());
                                end = pos + h - 4;
                                out = base;
                            }
                            commands(end, half(kStride));
                            index = (half(kStream) + 1) & 0xFFFF;
                            base++;
                            set_half(kStream, index);
                            if ((int32_t)index >= (int32_t)half(kStride)) {
                                break;
                            }
                        }
                    }
                }
                // The next chunk starts at the next 16-byte ROM address.
                uint32_t misaligned = end & 0xF;
                end++;
                while (misaligned != 0) {
                    advance();
                    misaligned = end & 0xF;
                    end++;
                }
                if ((int32_t)left <= 0) {
                    break;
                }
                first = peek();
            }
        }
        set_word(kDmaRom, dma_rom);
        set_word(kPos, pos);
        set_word(kDmaLeft, dma_left);
        set_word(kLeft, left);
        set_word(kRingLeft, ring_left);
        return out;
    }
};

}   // namespace

// u8 *recomp_decompress_file(u32 rom_addr, u8 *dst, u32 size)
extern "C" void recomp_decompress_file(uint8_t* rdram, recomp_context* ctx) {
    Lzkn64 lz{};
    lz.rdram = rdram;
    const uint32_t end = lz.run((uint32_t)ctx->r4, (uint32_t)ctx->r5, (uint32_t)ctx->r6);
    ctx->r2 = kseg0(end);
}
