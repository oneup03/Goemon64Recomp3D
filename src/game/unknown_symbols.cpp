#include "recomp.h"
#include "ultramodern/ultramodern.hpp"

extern "C" void __osPfsSelectBank_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void osPiWriteIo_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void osPiReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void __osViSwapContext_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void __osTimerInterrupt_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void rmonPrintf_recomp(uint8_t* rdram, recomp_context* ctx) {}

// The recompiled code calls these TLB functions, which upstream librecomp's
// ultra_stubs.cpp does not stub (it has only osUnmapTLBAll). The recompiled
// code addresses memory directly, so mapping has nothing to do.
extern "C" void osMapTLB_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void osUnmapTLB_recomp(uint8_t* rdram, recomp_context* ctx) {}
