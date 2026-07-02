/*
    sms_state.h  --  EKA1 heap-state container for E32SMS

    On EKA1 a Symbian .app loads as a DLL, and DLLs may not have writable
    static data. Every engine global written at runtime is moved into this
    single struct, which lives on the heap. A pointer to it is held in a
    MEMBER of the document (CE32FrodoDocument::iSmsState), never in a global.
    Engine code reaches the struct through SMS_State(), which resolves the
    document via the live AppUi.

    This header declares only the struct + accessor + the tms_sprite type.
    The per-global access macros (#define cart (SMS_State()->cart) etc.) live
    in the individual engine headers, gated so the struct itself can still
    name the real members.

    Two copies of most engine headers exist (src/ and smsplus/). This file
    lives in smsplus/.
*/
#ifndef _SMS_STATE_H_
#define _SMS_STATE_H_

/* Engine typedefs we aggregate. While building THIS struct we set
   SMS_STATE_DEFINING so the access macros in those headers stay inert and the
   member names below are real identifiers. */
#include "types.h"      /* uint8, uint16, uint32 -- needed standalone (C++) */
#define SMS_STATE_DEFINING 1
#include "system.h"     /* bitmap_t, cart_t, input_t */
#include "sms.h"        /* sms_t                     */
#include "vdp.h"        /* vdp_t                     */
#include "pio.h"        /* io_state                  */
#include "cpu/z80.h"    /* Z80_Regs, PAIR            */
#undef SMS_STATE_DEFINING

#ifdef __cplusplus
extern "C" {
#endif

#define SMSST_PALETTE_SIZE   0x20
#ifndef SMSST_PATH_MAX
#define SMSST_PATH_MAX       1024
#endif

/* Latched sprite data in the VDP. Moved here from tms.c so the state struct
   can hold a real array. tms.c includes this and drops its local typedef. */
typedef struct {
    int   xpos;
    uint8 attr;
    uint8 sg[2];
} tms_sprite;

typedef struct SMS_State
{
    /* ---- system.c ---- */
    bitmap_t  bitmap;            /* bitmap.data allocated separately */
    cart_t    cart;             /* cart.rom  allocated separately   */
    input_t   input;

    /* ---- sms.c ---- */
    sms_t     sms;
    uint8     dummy_write[0x400];
    uint8     dummy_read[0x400];

    /* ---- vdp.c ---- */
    vdp_t     vdp;

    /* ---- memz80.c ---- */
    uint8     data_bus_pullup;
    uint8     data_bus_pulldown;

    /* ---- loadrom.c ---- */
    char      game_name[SMSST_PATH_MAX];

    /* ---- render.c ---- */
    uint8     sms_cram_expand_table[4];
    uint8     gg_cram_expand_table[16];
    void    (*render_bg)(int line);    /* assigned at render_reset */
    void    (*render_obj)(int line);
    uint8*    linebuf;                  /* points into internal_buffer */
    /* E32SMS / EKA1 fix: render_bg_sms() writes into linebuf with a negative
       (0 - shift) underflow of up to 8 bytes AND dword writes for the 32nd
       column that reach ~8 bytes past the 256-pixel visible width. The visible
       SMS line is 256 px, but the renderer needs padding on both ends. The
       original 0x100 buffer was exactly the visible width with no slack, so on
       a frame where the background is enabled the writes spilled past the end.
       On WINS the adjacent heap absorbed it; on EKA1 it hits a page boundary
       and panics KERN-EXEC 3. 0x200 gives generous head/tail padding.
       NOTE: linebuf is pointed at &internal_buffer[8] (not [0]) in render_line
       so the (0 - shift) underflow stays inside the buffer. */
    uint8     internal_buffer[0x200];
    uint16    pixel[SMSST_PALETTE_SIZE];
    uint8     bg_name_dirty[0x200];
    uint16    bg_name_list[0x200];
    uint16    bg_list_index;
    uint8     bg_pattern_cache[0x20000];/* 128 KB */
    uint8     lut[0x10000];             /* 64 KB  */
    uint32    bp_lut[0x10000];          /* 256 KB */

    /* ---- tms.c ---- */
    int       text_counter;
    uint8     tms_lookup[16][256][2];
    uint8     mc_lookup[16][256][8];
    uint8     txt_lookup[256][2];
    uint8     bp_expand[256][8];
    uint8     tms_obj_lut[16*256];
    int       sprites_found;
    tms_sprite sprites[4];

    /* ---- pio.c ---- */
    io_state  io_lut[2][256];
    io_state *io_current;

    /* ---- debug: bounded per-opcode trace counter (set in SmsInitL) ---- */
    int       z80trace;

    /* ---- z80.c ---- */
    Z80_Regs  z80regs;                  /* the CPU register file */
    int       z80_exec;
    int       z80_cycle_count;
    int       z80_requested_cycles;
    int       z80_ICount;
    int       after_EI;
    uint32    EA;
    uint8     SZ[256];
    uint8     SZ_BIT[256];
    uint8     SZP[256];
    uint8     SZHV_inc[256];
    uint8     SZHV_dec[256];
    uint8*    SZHVC_add;                 /* 2*256*256, alloc separately */
    uint8*    SZHVC_sub;                 /* 2*256*256, alloc separately */
    uint8*    cpu_readmap[64];
    uint8*    cpu_writemap[64];
    void   (*cpu_writemem16)(int address, int data);
    void   (*cpu_writeport16)(uint16 port, uint8 data);
    uint8  (*cpu_readport16)(uint16 port);
    Z80_Regs* Z80_Context;              /* = &z80regs after init */

} SMS_State_t;

/* Resolves CCoeEnv::Static()->AppUi()->Document()->SmsState().
   Valid wherever AppUi is alive (inside SmsTick and all it calls).
   NOT valid in the document's ConstructL (AppUi not yet created). */
#ifndef SMS_GLOBALS
SMS_State_t* SMS_State(void);
#endif /* !SMS_GLOBALS */

#ifdef __cplusplus
}
#endif

/* ------------------------------------------------------------------ */
/* Access macros for globals that have NO declaration in a public      */
/* engine header (they were file-scope statics/globals in their .c).   */
/* These are only meaningful in the engine .c files, never while the    */
/* struct is being defined.                                            */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Access macros.  These redirect the engine's old global names to the */
/* heap struct.  They are ONLY for the pure-C engine translation units. */
/* In C++ TUs (the bridge, the AppUi) they must stay INACTIVE, because  */
/* there we reference struct members explicitly as st->bitmap etc., and */
/* a `bitmap` macro would corrupt `st->bitmap` into `st->(SMS_State()...`*/
/* ------------------------------------------------------------------ */
#if defined(SMS_GLOBALS)
/* EXE build: real globals, declared extern here. */
#include "sms_extern.h"
#elif !defined(SMS_STATE_DEFINING) && !defined(__cplusplus)

/* ---- system.c ---- */
#define bitmap (SMS_State()->bitmap)
#define cart   (SMS_State()->cart)
#define input  (SMS_State()->input)

/* ---- sms.c ---- */
#define sms         (SMS_State()->sms)
#define dummy_write (SMS_State()->dummy_write)
#define dummy_read  (SMS_State()->dummy_read)

/* ---- vdp.c ---- */
#define vdp (SMS_State()->vdp)

/* ---- memz80.c ---- */
#define data_bus_pullup   (SMS_State()->data_bus_pullup)
#define data_bus_pulldown (SMS_State()->data_bus_pulldown)

/* ---- loadrom.c ---- */
#define game_name (SMS_State()->game_name)

/* ---- render.c ---- */
#define sms_cram_expand_table (SMS_State()->sms_cram_expand_table)
#define gg_cram_expand_table  (SMS_State()->gg_cram_expand_table)
#define render_bg             (SMS_State()->render_bg)
#define render_obj            (SMS_State()->render_obj)
#define linebuf               (SMS_State()->linebuf)
#define internal_buffer       (SMS_State()->internal_buffer)
#define pixel                 (SMS_State()->pixel)
#define bg_name_dirty         (SMS_State()->bg_name_dirty)
#define bg_name_list          (SMS_State()->bg_name_list)
#define bg_list_index         (SMS_State()->bg_list_index)
#define bg_pattern_cache      (SMS_State()->bg_pattern_cache)
#define tms_lookup            (SMS_State()->tms_lookup)
#define mc_lookup             (SMS_State()->mc_lookup)
#define txt_lookup            (SMS_State()->txt_lookup)
#define bp_expand             (SMS_State()->bp_expand)
#define lut                   (SMS_State()->lut)
#define bp_lut                (SMS_State()->bp_lut)

/* ---- tms.c ---- */
#define text_counter  (SMS_State()->text_counter)
#define tms_obj_lut   (SMS_State()->tms_obj_lut)
#define sprites_found (SMS_State()->sprites_found)
#define sprites       (SMS_State()->sprites)

/* ---- pio.c ---- */
#define io_lut     (SMS_State()->io_lut)
#define io_current (SMS_State()->io_current)

/* z80.c globals with LONG, collision-safe names -- these are also used by
   sms.c / memz80.c / state.c, so they live in this shared header.  The
   SHORT/risky z80 names (Z80, EA, SZ, ...) are mapped only in z80_state.h,
   which z80.c alone includes. */
#define Z80_Context          (SMS_State()->Z80_Context)
#define z80_exec             (SMS_State()->z80_exec)
#define z80_cycle_count      (SMS_State()->z80_cycle_count)
#define z80_requested_cycles (SMS_State()->z80_requested_cycles)
#define cpu_readmap          (SMS_State()->cpu_readmap)
#define cpu_writemap         (SMS_State()->cpu_writemap)
#define cpu_writemem16       (SMS_State()->cpu_writemem16)
#define cpu_writeport16      (SMS_State()->cpu_writeport16)
#define cpu_readport16       (SMS_State()->cpu_readport16)
#define after_EI             (SMS_State()->after_EI)

/* The SHORT-name Z80-internal globals (Z80, EA, SZ, after_EI, ...) are NOT
   mapped here, to avoid leaking very short macro names into C++ TUs and
   Symbian headers.  They are mapped in z80_state.h (z80.c only). */

#endif /* !SMS_STATE_DEFINING */

#endif /* _SMS_STATE_H_ */
