#include <stdio.h>

#ifdef __3DS__
// The 3DS build uses the per-function hook as its preemption check: when a
// host thread has queued an external event (a VI retrace, a task's
// completion), the runtime delivers it here and switches to a woken
// higher-priority thread, as the console's interrupt would have. Without
// this a long stretch of game logic held the scheduler and the audio
// thread back until it next touched a message queue, which at the Old 3DS
// clock cost half the audio frames (see ultramodern.hpp).
extern unsigned int ultramodern_preempt_pending;
extern void ultramodern_preempt(unsigned char* rdram);
// The function running, for rt64-3ds's sampling profile of the game
// (settings.ini's dbg_profile=2, game_profile.h): set at entry, the
// caller's put back at return, so a sample names the function executing.
extern const char* volatile recomp3ds_trace_func;
#define TRACE_ENTRY() \
    if (*(volatile unsigned int*)&ultramodern_preempt_pending) { \
        ultramodern_preempt(rdram); \
    } \
    const char* const trace_prev_ = recomp3ds_trace_func; \
    recomp3ds_trace_func = __func__;
#define TRACE_RETURN() \
    recomp3ds_trace_func = trace_prev_;

#else
#define TRACE_ENTRY() \
    static int was_called = 0; \
    if (was_called == 0) { \
        was_called = 1; \
    } \

#define TRACE_RETURN() \
    return; \

#endif
