/*
    sms_globals.c  --  Stage 0 of the EXE migration.

    In the EXE build, writable static data is allowed, so the entire emulator
    state becomes plain globals instead of a heap struct reached through
    SMS_State(). This file DEFINES every former state member as a real global,
    with exactly the names the engine code expects.

    Build model:
      * EXE build defines SMS_GLOBALS.
      * sms_state.h, when SMS_GLOBALS is defined, MUST NOT emit the
        `#define sms (SMS_State()->sms)` redirection macros (so the names below
        are real globals), and MUST NOT declare SMS_State()/the struct.
      * Engine .c files include their headers as usual; `sms`, `vdp`, `cart`,
        etc. now resolve to these globals directly. Zero engine-code changes.

    Pointer buffers that the engine allocates separately at runtime
    (bitmap.data, cart.rom, SZHVC_add/sub) are left as-is: they are members of
    the structs/globals here but their backing memory is malloc'd by the engine
    (z80.c Z80_InitTables, loadrom/appui). That logic is unchanged.

    The big lookup tables (bp_lut 256KB, bg_pattern_cache 128KB, lut 64KB,
    SZHVC via malloc) are fine as globals/.bss in an EXE.

    NOTE: matches SMS_State_t field list in sms_state.h exactly. If that struct
    changes, update here too.
*/

#include "types.h"
#define SMS_STATE_DEFINING 1   /* pull in the real struct typedefs, not macros */
#include "system.h"   /* bitmap_t, cart_t, input_t */
#include "sms.h"      /* sms_t                     */
#include "vdp.h"      /* vdp_t                     */
#include "pio.h"      /* io_state                  */
#include "cpu/z80.h"  /* Z80_Regs, PAIR            */
#undef SMS_STATE_DEFINING

#include "sms_state.h" /* for tms_sprite typedef + SMSST_* sizes */

/* ---- system.c ---- */
bitmap_t  bitmap;
cart_t    cart;
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
void    (*render_bg)(int line);
void    (*render_obj)(int line);
uint8*    linebuf;
uint8     internal_buffer[0x200];
uint16    pixel[SMSST_PALETTE_SIZE];
uint8     bg_name_dirty[0x200];
uint16    bg_name_list[0x200];
uint16    bg_list_index;
uint8     bg_pattern_cache[0x20000]; /* 128 KB */
uint8     lut[0x10000];              /* 64 KB  */
uint32    bp_lut[0x10000];           /* 256 KB */

/* ---- tms.c ---- */
int        text_counter;
uint8      tms_lookup[16][256][2];
uint8      mc_lookup[16][256][8];
uint8      txt_lookup[256][2];
uint8      bp_expand[256][8];
uint8      tms_obj_lut[16*256];
int        sprites_found;
tms_sprite sprites[4];

/* ---- pio.c ---- */
io_state   io_lut[2][256];
io_state  *io_current;

/* ---- z80.c ---- */
Z80_Regs   z80regs;
int        z80_exec;
int        z80_cycle_count;
int        z80_requested_cycles;
int        z80_ICount;
int        after_EI;
uint32     EA;
uint8      SZ[256];
uint8      SZ_BIT[256];
uint8      SZP[256];
uint8      SZHV_inc[256];
uint8      SZHV_dec[256];
uint8*     SZHVC_add;   /* malloc'd in Z80_InitTables */
uint8*     SZHVC_sub;   /* malloc'd in Z80_InitTables */
uint8*     cpu_readmap[64];
uint8*     cpu_writemap[64];
void    (*cpu_writemem16)(int address, int data);
void    (*cpu_writeport16)(uint16 port, uint8 data);
uint8   (*cpu_readport16)(uint16 port);
Z80_Regs*  Z80_Context;

/* ---- E32SMS direct-to-VRAM output hooks (declared in render.h) ----
   Set by the EXE main under focus gating. When g_vram_enable && g_vram_base,
   remap_8_to_16() writes RGB444 straight to video memory, replacing the
   separate blit pass. */
unsigned char *g_vram_base   = 0;
int            g_vram_stride = 0;
int            g_vram_dx     = 0;
int            g_vram_dy     = 0;
int            g_vram_enable = 0;
