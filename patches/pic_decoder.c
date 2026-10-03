#include "patches.h"

// The game's "PIC" image decoder (func_80014D70_15970 and func_80014DD8_159D8,
// which func_800144E8_150E8 runs on a loaded file that starts with "PIC") makes
// no OS call while it decodes. On the N64 the audio thread preempts it at the
// next interrupt; in the runtime, threads only switch at OS calls, so the audio
// thread waited out every decode, which on the 3DS takes a quarter of a second
// for a full-screen image, and the DSP ran dry at the scene changes.
//
// Now the decoder offers the higher-priority threads a turn every so often,
// through a receive on a queue of its own that never blocks: a thread switch
// point as every OS call is, at a place the N64 could have preempted it too.
// From its bit reader (wave_uty.c; pic_decoder_bits.s), which every kind of
// image goes through, and from the two helpers of the bitmap of decoded pixels
// that the 8-bit decoder's passes over the whole image call (func_80014F1C_15B1C)
// without reading any input. The patched functions do what the originals do,
// and on their usual path cost a few instructions more.

extern s32 D_8006ADC4_6B9C4;    // bits left in the current byte
extern s32 D_8006ADCC_6B9CC;    // the current byte
extern u8 *D_80168160_168D60;   // the next byte

// The bit reader (pic_decoder_bits.s) yields every DECODE_YIELD_CALLS reads, the
// bitmap helpers at every DECODE_YIELD_CALLS-th pixel: about every 1-2 ms of
// decoding on the 3DS.
#define DECODE_YIELD_CALLS 2048

static OSMesgQueue decode_yield_queue;
static OSMesg decode_yield_msg;
static s32 decode_yield_ready;
u32 decode_yield_calls;     // counted by pic_decoder_bits.s

static __attribute__((noinline)) void decode_yield(void) {
    if (!decode_yield_ready) {
        osCreateMesgQueue(&decode_yield_queue, &decode_yield_msg, 1);
        decode_yield_ready = TRUE;
    }
    osRecvMesg(&decode_yield_queue, NULL, OS_MESG_NOBLOCK);
}

// The originals, as they are.
static inline s32 read_bits(s32 count) {
    s32 left = D_8006ADC4_6B9C4;
    u32 bits = 0;

    if (left < count) {
        do {
            u32 cur = D_8006ADCC_6B9CC;
            u8 *src = D_80168160_168D60;
            u8 next = *src;

            count -= left;
            bits |= (cur & ((1u << (left & 31)) - 1)) << (count & 31);
            D_80168160_168D60 = src + 1;
            D_8006ADC4_6B9C4 = 8;
            left = 8;
            D_8006ADCC_6B9CC = next;
        } while (count > 8);
    }
    D_8006ADC4_6B9C4 = left - count;
    return (((u32)D_8006ADCC_6B9CC >> ((left - count) & 31)) & ((1u << (count & 31)) - 1)) | bits;
}

static inline u8 *clear_bit(u8 *map, s32 index) {
    u8 *p = map + (index >> 3);

    *p = *p & ~(1 << (index & 7));
    return p;
}

static inline s32 test_bit(u8 *map, s32 index) {
    return (map[index >> 3] & (1 << (index & 7))) != 0;
}

// The yielding paths, apart so that the patched functions stay leaves on
// their usual path, nearly as cheap as the originals.

// pic_decoder_bits.s jumps here with the count it was given.
s32 read_bits_after_yield(s32 count) {
    decode_yield_calls = 0;
    decode_yield();
    return read_bits(count);
}

static __attribute__((noinline, cold)) u8 *clear_bit_after_yield(u8 *map, s32 index) {
    decode_yield();
    return clear_bit(map, index);
}

static __attribute__((noinline, cold)) s32 test_bit_after_yield(u8 *map, s32 index) {
    decode_yield();
    return test_bit(map, index);
}

// @recomp The bitmap of decoded pixels, a bit per pixel: clear (returning the
// byte's address, as the original leaves it in v0) and test, as the originals
// do, and yield now and then. The 8-bit decoder (func_80014F1C_15B1C) clears
// the whole bitmap and later tests every bit in passes that read no input.
RECOMP_PATCH u8 *func_80016684_17284(u8 *map, s32 index) {
    if ((index & (DECODE_YIELD_CALLS - 1)) == DECODE_YIELD_CALLS - 1) {
        return clear_bit_after_yield(map, index);
    }
    return clear_bit(map, index);
}

RECOMP_PATCH s32 func_800166AC_172AC(u8 *map, s32 index) {
    if ((index & (DECODE_YIELD_CALLS - 1)) == DECODE_YIELD_CALLS - 1) {
        return test_bit_after_yield(map, index);
    }
    return test_bit(map, index);
}
