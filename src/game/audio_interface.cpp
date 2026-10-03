// The audio interface functions of upstream librecomp's ai.cpp, with the DAC
// rate rounding that file leaves commented out turned on, as the goemon-patches
// fork of N64ModernRuntime had it. Goemon paces its audio thread on
// osAiGetLength, and the desktop build has always run with the frequency the
// console's integer DAC divider actually produces (a 32000 Hz request, for
// example, runs at 32006 Hz).
//
// librecomp is a static library, and this file defines every symbol that its
// ai.cpp object exports. The linker resolves the recompiled code's references
// to this executable's definitions first, so it never extracts librecomp's
// ai.o from the archive and there is no duplicate definition. Keep the two
// sets equal: should upstream add a symbol to ai.cpp, define it here too, or
// the archive member would be pulled in and clash.

#include "recomp.h"
#include <ultramodern/ultra64.h>
#include <ultramodern/ultramodern.hpp>

#define VI_NTSC_CLOCK 48681812

extern "C" void osAiSetFrequency_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint32_t freq = ctx->r4;
    // Round to the frequency the console's DAC divider produces.
    uint32_t dacRate = (uint32_t)(((float)VI_NTSC_CLOCK / freq) + 0.5f);
    freq = VI_NTSC_CLOCK / dacRate;
    ctx->r2 = freq;
    ultramodern::set_audio_frequency(freq);
}

extern "C" void osAiSetNextBuffer_recomp(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::queue_audio_buffer(rdram, ctx->r4, ctx->r5);
    ctx->r2 = 0;
}

extern "C" void osAiGetLength_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = ultramodern::get_remaining_audio_bytes();
}

extern "C" void osAiGetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0x00000000; // Pretend the audio DMAs finish instantly
}
