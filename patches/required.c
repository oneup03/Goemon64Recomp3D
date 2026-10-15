#include "patches.h"
#include "misc_funcs.h"
#include "overlay_files.h"
#include "graphics.h"

extern void overlay_apply_relocations(u32 file_id, u8 *load_addr);
extern void *func_80014840_15440(void *, u32 file_id);
extern void *func_80013B14_14714(unsigned short file_id);
extern Task *D_8006D328_6DF28;
extern void* func_080026B8_71FE88(void *arg);

// @recomp The recompiled overlays come from the decompressed ROM layout, while the
// game reads its files from the retail ROM (decompressing them itself, as on the
// N64), so a file's ROM address is not its overlay's. Register a code file's
// overlay by its file id instead (overlay_files.h); other files have no code.
static void load_file_overlays(u32 file_id, u8 *buf_start)
{
    u32 i;

    for (i = 0; i < sizeof(overlay_files) / sizeof(overlay_files[0]); i++) {
        if (overlay_files[i].file_id == file_id) {
            recomp_load_overlays(overlay_files[i].rom, (void *)buf_start, overlay_files[i].size);
            return;
        }
    }
}

// @recomp File 14 holds 14 render-state display lists (ROM 0x65D290..0x65D890)
// that the stage's objects call before drawing, and each sets the scissor to
// (0,16)-(320,224), the N64's overscan border: they undid the camera's widened
// scissor (anime.c, widen_camera_scissor) and the ground ended 16 rows above the
// bottom of a wide screen, the sky showing below it. Its loaded copy gets the
// same rule. No other file has such static scissors.
#define FILE_STAGE_RENDER_STATES 14

static void widen_file_scissors(u32 file_id, u8 *start, u8 *end)
{
    const f32 original_aspect_ratio = (f32)SCREEN_WIDTH / (f32)SCREEN_HEIGHT;
    Gfx *g;

    if ((file_id != FILE_STAGE_RENDER_STATES) || (((u32)start & 7) != 0) ||
        (recomp_get_target_aspect_ratio(original_aspect_ratio) == original_aspect_ratio)) {
        return;
    }

    for (g = (Gfx *)start; (u8 *)(g + 1) <= end; g++) {
        widen_overscan_scissor(g);
    }
}

// @recomp Patched to load the overlay in the recomp runtime and to relocate TLB mapped overlays.
RECOMP_PATCH u8 *func_80001C00_2800(u32 file_id, u8 *buf_start) 
{
    s32 cur_file_id;
    s32 next_file_id;
    u32 file_rom_addr;
    u32 file_size;
    u32 file_size_aligned;
    u32 vram_start;
    u32 vram_end;
    u8 *buf_cur;
    u8 *buf_end;
    u8 was_loading_file;

    was_loading_file = D_8015C5D4_15D1D4;

    if (file_id < FILE_MAX && file_id != FILE_NONE) {
        cur_file_id = file_id - 1;
        next_file_id = file_id;
        file_rom_addr = D_800573D8_57FD8[cur_file_id] & FILE_ADDR_MASK;
        file_size = (D_800573D8_57FD8[next_file_id] & FILE_ADDR_MASK) - file_rom_addr;
        vram_start = D_80054ACC_556CC[cur_file_id].start;
        vram_end = D_80054ACC_556CC[cur_file_id].end;

        buf_end = buf_start + ((s32)vram_end - (s32)vram_start);

        D_8015C5D4_15D1D4 = TRUE;
        
        buf_cur = buf_start;

        if (file_size != 0) {
            // @recomp Load the overlay in the recomp runtime.
            load_file_overlays(file_id, buf_start);
            
            if (D_800573D8_57FD8[cur_file_id] & FILE_COMP_MASK) {
                buf_cur = func_80005394_5F94(file_rom_addr, buf_start, file_size);
            } else {
                file_size_aligned = (file_size + 1) & ~1;
                func_80001640_2240(file_rom_addr, (void *)buf_start, file_size_aligned);
                buf_cur = buf_start + file_size_aligned;
            }

            // @recomp Apply relocations after loading the overlay.
            overlay_apply_relocations(file_id, buf_start);

            // @recomp Widen the overscan scissors of its display lists.
            widen_file_scissors(file_id, buf_start, buf_cur);
        }

        D_8015C5D4_15D1D4 = was_loading_file;

        while (buf_cur < buf_end) {
            *buf_cur = 0;
            buf_cur++;
        }

        // @recomp Not needed in the static recompilation.
        // osWritebackDCache_recomp(buf_start, buf_end - buf_start);
    } else {
        buf_end = NULL;
    }

    // Sometimes used to determine where to next load a file.
    return buf_end;
}

// @recomp The game's LZKN64 decompressor ("memory_decompress") on the host
// (src/game/file_decompression.cpp): recompiled, it spent a function call and a
// dozen RAM accesses on every input byte, a seventh of a second for the
// largest file on the 3DS, which every scene change loads. Same input reads
// into the same ring buffer, same output, state and return value.
RECOMP_PATCH u8 *func_80005394_5F94(u32 rom_addr, u8 *vram_addr, u32 size)
{
    return recomp_decompress_file(rom_addr, vram_addr, size);
}

// @recomp Patched to relocate TLB mapped addresses to their proper KSEG0 addresses so that the recomp runtime can find them.
RECOMP_PATCH void func_8001481C_1541C(Task *task) 
{
    const u32 tlb_overlay_start = 0x08000000lu;
    const u32 code_overlay_mask = ~0x40000000;

    func_80001EB0_2AB0(task->overlay_file_id, (u32)task->overlay_vram_addr);

    // @recomp Relocate all functions pointers.
    if (task->function_1 != NULL) {
        // If task->overlay_file_id is non-zero and loaded into memory, this function will resolve task->function_1 into a KSEG0 pointer.
        void *resolved_function = func_80014840_15440((void *)task->function_1, task->overlay_file_id);

        // If task->overlay_file_id is non-zero and loaded into memory, this function will return the base address of the overlay as a KSEG0 pointer.
        // Note that the returned pointer to the start of the overlay will be ORed with 0x40000000 if it is an overlay containing code.
        u32 overlay_start = (u32)func_800141C4_14DC4(task->overlay_file_id) & code_overlay_mask;

        if (resolved_function != NULL && resolved_function != task->function_1) {
            task->function_1 = (TaskCallback) ((u32)task->function_1 - tlb_overlay_start + overlay_start);
        }
    }

    if (task->function_2 != NULL) {
        void *resolved_function = func_80014840_15440((void *)task->function_2, task->overlay_file_id);
        u32 overlay_start = (u32)func_800141C4_14DC4(task->overlay_file_id) & code_overlay_mask;

        if (resolved_function != NULL && resolved_function != task->function_2) {
            task->function_2 = (TaskCallback) ((u32)task->function_2 - tlb_overlay_start + overlay_start);
        }
    }

    if (task->function_3 != NULL) {
        void *resolved_function = func_80014840_15440((void *)task->function_3, task->overlay_file_id);
        u32 overlay_start = (u32)func_800141C4_14DC4(task->overlay_file_id) & code_overlay_mask;

        if (resolved_function != NULL && resolved_function != task->function_3) {
            task->function_3 = (TaskCallback) ((u32)task->function_3 - tlb_overlay_start + overlay_start);
        }
    }
}

// @recomp Patch the only other place where a TLB mapped overlay function is called from the main segment.
RECOMP_PATCH void func_8001198C_1258C(void) 
{
    func_80013B14_14714(0x3C);
    func_80001EB0_2AB0(0x3C, (u32)func_800141C4_14DC4(0x3C));
    func_080026B8_71FE88(D_8006D328_6DF28);
}