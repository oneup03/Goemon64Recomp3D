// Force-included into RecompiledFuncs only.
#ifndef GOEMON_HOST_PROTOS_H
#define GOEMON_HOST_PROTOS_H

#include "recomp.h"

// The originals in RecompiledFuncs must yield to the patched versions in
// RecompiledPatches. Clang's RECOMP_FUNC is weak, which gives that for free;
// GCC's is not, so this header redefines it weak for the originals. The
// patches keep the strong symbol. (mnsg.toml hooks no host functions into
// the game's code, so there are no prototypes to declare here.)
#undef RECOMP_FUNC
#define RECOMP_FUNC __attribute__((weak, noipa, optimize("rounding-math")))

#endif
